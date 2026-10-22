#include "audio_player.h"

#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"

#define AUDIO_I2S_PORT I2S_NUM_0
#define AUDIO_I2C_PORT I2C_NUM_0
#define AUDIO_MCLK_GPIO 8
#define AUDIO_BCLK_GPIO 9
#define AUDIO_LRCK_GPIO 10
#define AUDIO_DOUT_GPIO 12
#define AUDIO_I2C_SDA_GPIO 42
#define AUDIO_I2C_SCL_GPIO 41
#define AUDIO_PA_GPIO 7
#define AUDIO_MCLK_MULTIPLE 256
#define AUDIO_VOLUME 65

static const char *TAG = "AUDIO";
static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static bool s_ready;
static uint8_t s_volume = AUDIO_VOLUME;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static esp_err_t wav_payload(const uint8_t *wav, size_t size,
                             const uint8_t **samples, size_t *sample_count)
{
    if (!wav || !samples || !sample_count || size < 44 || size > AUDIO_WAV_MAX_BYTES ||
        memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bool fmt_ok = false;
    size_t offset = 12;
    while (offset + 8 <= size) {
        uint32_t chunk_size = le32(wav + offset + 4);
        size_t data_offset = offset + 8;
        if (data_offset + chunk_size > size) return ESP_ERR_INVALID_SIZE;
        if (memcmp(wav + offset, "fmt ", 4) == 0) {
            if (chunk_size < 16 || le16(wav + data_offset) != 1 ||
                le16(wav + data_offset + 2) != 1 ||
                le32(wav + data_offset + 4) != AUDIO_WAV_SAMPLE_RATE ||
                le16(wav + data_offset + 14) != 8) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            fmt_ok = true;
        } else if (memcmp(wav + offset, "data", 4) == 0) {
            if (!fmt_ok || chunk_size == 0 ||
                chunk_size > AUDIO_WAV_SAMPLE_RATE * AUDIO_WAV_MAX_SECONDS) {
                return ESP_ERR_INVALID_SIZE;
            }
            *samples = wav + data_offset;
            *sample_count = chunk_size;
            return ESP_OK;
        }
        offset = data_offset + chunk_size + (chunk_size & 1U);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t audio_player_validate_wav(const uint8_t *wav, size_t size)
{
    const uint8_t *samples;
    size_t count;
    return wav_payload(wav, size, &samples, &count);
}

esp_err_t audio_player_init(void)
{
    if (s_ready) return ESP_OK;

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    esp_err_t err = i2s_new_channel(&channel, &s_tx, NULL);
    if (err != ESP_OK) return err;

    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_WAV_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_MCLK_GPIO,
            .bclk = AUDIO_BCLK_GPIO,
            .ws = AUDIO_LRCK_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard.clk_cfg.mclk_multiple = AUDIO_MCLK_MULTIPLE;
    err = i2s_channel_init_std_mode(s_tx, &standard);
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) return err;

    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = AUDIO_I2C_PORT,
        .sda_io_num = AUDIO_I2C_SDA_GPIO,
        .scl_io_num = AUDIO_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_config, &i2c_bus);
    if (err != ESP_OK) return err;

    audio_codec_i2c_cfg_t control_config = {
        .port = AUDIO_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *control = audio_codec_new_i2c_ctrl(&control_config);
    audio_codec_i2s_cfg_t data_config = {
        .port = AUDIO_I2S_PORT,
        .rx_handle = NULL,
        .tx_handle = s_tx,
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_config);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!control || !data || !gpio) return ESP_ERR_NO_MEM;

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = control,
        .gpio_if = gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = AUDIO_PA_GPIO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .mclk_div = AUDIO_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_config);
    if (!codec_if) return ESP_FAIL;

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data,
    };
    s_codec = esp_codec_dev_new(&device_config);
    if (!s_codec) return ESP_FAIL;

    esp_codec_dev_sample_info_t sample = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = AUDIO_WAV_SAMPLE_RATE,
    };
    if (esp_codec_dev_open(s_codec, &sample) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(s_codec, s_volume) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_mute(s_codec, true) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    s_ready = true;
    ESP_LOGI(TAG,
             "ES8311 listo: MCLK=8 BCLK=9 LRCK=10 DSDIN=12 SDA=42 SCL=41 PA=7");
    return ESP_OK;
}

bool audio_player_ready(void)
{
    return s_ready;
}

uint8_t audio_player_get_volume(void)
{
    return s_volume;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (volume > 100) return ESP_ERR_INVALID_ARG;
    if (esp_codec_dev_set_out_vol(s_codec, volume) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    s_volume = volume;
    ESP_LOGI(TAG, "Volumen: %u%%", (unsigned)volume);
    return ESP_OK;
}

esp_err_t audio_player_play_wav(const uint8_t *wav, size_t size)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    const uint8_t *samples;
    size_t count;
    esp_err_t err = wav_payload(wav, size, &samples, &count);
    if (err != ESP_OK) return err;

    int16_t *stereo = malloc(512U * 2U * sizeof(int16_t));
    if (!stereo) return ESP_ERR_NO_MEM;
    if (esp_codec_dev_set_out_mute(s_codec, false) != ESP_CODEC_DEV_OK) {
        free(stereo);
        return ESP_FAIL;
    }
    size_t offset = 0;
    while (offset < count) {
        size_t frames = count - offset > 512 ? 512 : count - offset;
        for (size_t i = 0; i < frames; ++i) {
            int16_t sample = ((int16_t)samples[offset + i] - 128) << 8;
            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;
        }
        int result = esp_codec_dev_write(s_codec, stereo,
                                         (int)(frames * 2 * sizeof(int16_t)));
        if (result != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            break;
        }
        offset += frames;
    }
    esp_codec_dev_set_out_mute(s_codec, true);
    free(stereo);
    ESP_LOGI(TAG, "WAV reproducido: %u muestras, %u ms", (unsigned)count,
             (unsigned)(count * 1000U / AUDIO_WAV_SAMPLE_RATE));
    return err;
}
