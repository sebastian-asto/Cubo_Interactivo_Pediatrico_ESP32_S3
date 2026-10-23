#include "audio_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_vol.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
#define AUDIO_FILE_PATH_MAX 128
#define AUDIO_STREAM_FRAMES 512U

static const char *TAG = "AUDIO";
static i2s_chan_handle_t s_tx;
static esp_codec_dev_handle_t s_codec;
static bool s_ready;
static uint8_t s_volume = AUDIO_VOLUME;
static QueueHandle_t s_play_queue;
static SemaphoreHandle_t s_codec_lock;
static portMUX_TYPE s_generation_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_play_generation;

typedef struct {
    uint8_t *wav;
    size_t size;
    uint32_t generation;
    char path[AUDIO_FILE_PATH_MAX];
} audio_request_t;

static void audio_task(void *argument);

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

static esp_err_t wav_file_payload(FILE *file, size_t file_size,
                                  long *samples_offset, size_t *sample_count)
{
    if (!file || !samples_offset || !sample_count || file_size < 44 ||
        file_size > AUDIO_WAV_MAX_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t header[12];
    if (fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bool fmt_ok = false;
    size_t offset = sizeof(header);
    while (offset + 8 <= file_size) {
        uint8_t chunk[8];
        if (fseek(file, (long)offset, SEEK_SET) != 0 ||
            fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            return ESP_FAIL;
        }
        uint32_t chunk_size = le32(chunk + 4);
        size_t data_offset = offset + sizeof(chunk);
        if (data_offset + chunk_size > file_size) return ESP_ERR_INVALID_SIZE;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t format[16];
            if (chunk_size < sizeof(format) ||
                fread(format, 1, sizeof(format), file) != sizeof(format)) {
                return ESP_ERR_INVALID_SIZE;
            }
            if (le16(format) != 1 || le16(format + 2) != 1 ||
                le32(format + 4) != AUDIO_WAV_SAMPLE_RATE ||
                le16(format + 14) != 8) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            fmt_ok = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!fmt_ok || chunk_size == 0 ||
                chunk_size > AUDIO_WAV_SAMPLE_RATE * AUDIO_WAV_MAX_SECONDS) {
                return ESP_ERR_INVALID_SIZE;
            }
            *samples_offset = (long)data_offset;
            *sample_count = chunk_size;
            return ESP_OK;
        }
        offset = data_offset + chunk_size + (chunk_size & 1U);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t inspect_wav_file(const char *path, FILE **opened,
                                  long *samples_offset, size_t *sample_count)
{
    if (!path || !path[0]) return ESP_ERR_INVALID_ARG;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    esp_err_t err = ESP_OK;
    if (fseek(file, 0, SEEK_END) != 0) {
        err = ESP_FAIL;
    } else {
        long end = ftell(file);
        if (end < 0) err = ESP_FAIL;
        else err = wav_file_payload(file, (size_t)end, samples_offset, sample_count);
    }
    if (err != ESP_OK || !opened) {
        fclose(file);
    } else {
        *opened = file;
    }
    return err;
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
    s_codec_lock = xSemaphoreCreateMutex();
    s_play_queue = xQueueCreate(1, sizeof(audio_request_t));
    if (!s_codec_lock || !s_play_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "cube_audio", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
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
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    int result = esp_codec_dev_set_out_vol(s_codec, volume);
    if (result == ESP_CODEC_DEV_OK) s_volume = volume;
    xSemaphoreGive(s_codec_lock);
    if (result != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Volumen: %u%%", (unsigned)volume);
    return ESP_OK;
}

static esp_err_t play_wav(const uint8_t *wav, size_t size, uint32_t generation,
                          bool cancellable)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    const uint8_t *samples;
    size_t count;
    esp_err_t err = wav_payload(wav, size, &samples, &count);
    if (err != ESP_OK) return err;

    int16_t *stereo = malloc(512U * 2U * sizeof(int16_t));
    if (!stereo) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    int codec_result = esp_codec_dev_set_out_mute(s_codec, false);
    xSemaphoreGive(s_codec_lock);
    if (codec_result != ESP_CODEC_DEV_OK) {
        free(stereo);
        return ESP_FAIL;
    }
    size_t offset = 0;
    bool cancelled = false;
    while (offset < count) {
        if (cancellable && generation != s_play_generation) {
            ESP_LOGI(TAG, "Reproduccion reemplazada por una emocion nueva");
            cancelled = true;
            break;
        }
        size_t frames = count - offset > 512 ? 512 : count - offset;
        for (size_t i = 0; i < frames; ++i) {
            int16_t sample =
                (int16_t)((int32_t)samples[offset + i] - 128) * 256;
            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;
        }
        xSemaphoreTake(s_codec_lock, portMAX_DELAY);
        int result = esp_codec_dev_write(s_codec, stereo,
                                         (int)(frames * 2 * sizeof(int16_t)));
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            break;
        }
        offset += frames;
    }
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    esp_codec_dev_set_out_mute(s_codec, true);
    xSemaphoreGive(s_codec_lock);
    free(stereo);
    ESP_LOGI(TAG, "WAV %s: %u/%u muestras, %u ms",
             cancelled ? "cancelado" : "reproducido", (unsigned)offset,
             (unsigned)count,
             (unsigned)(offset * 1000U / AUDIO_WAV_SAMPLE_RATE));
    return err;
}

static esp_err_t play_wav_file(const char *path, uint32_t generation)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    FILE *file = NULL;
    long samples_offset = 0;
    size_t count = 0;
    esp_err_t err = inspect_wav_file(path, &file, &samples_offset, &count);
    if (err != ESP_OK) return err;

    uint8_t *mono = malloc(AUDIO_STREAM_FRAMES);
    int16_t *stereo = malloc(AUDIO_STREAM_FRAMES * 2U * sizeof(int16_t));
    if (!mono || !stereo) {
        free(mono);
        free(stereo);
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    if (fseek(file, samples_offset, SEEK_SET) != 0) err = ESP_FAIL;
    if (err == ESP_OK) {
        xSemaphoreTake(s_codec_lock, portMAX_DELAY);
        int result = esp_codec_dev_set_out_mute(s_codec, false);
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_CODEC_DEV_OK) err = ESP_FAIL;
    }

    size_t offset = 0;
    bool cancelled = false;
    while (err == ESP_OK && offset < count) {
        if (generation != s_play_generation) {
            cancelled = true;
            break;
        }
        size_t frames = count - offset > AUDIO_STREAM_FRAMES
            ? AUDIO_STREAM_FRAMES : count - offset;
        if (fread(mono, 1, frames, file) != frames) {
            err = ESP_FAIL;
            break;
        }
        for (size_t i = 0; i < frames; ++i) {
            int16_t sample = (int16_t)((int32_t)mono[i] - 128) * 256;
            stereo[i * 2] = sample;
            stereo[i * 2 + 1] = sample;
        }
        xSemaphoreTake(s_codec_lock, portMAX_DELAY);
        int result = esp_codec_dev_write(s_codec, stereo,
                                         (int)(frames * 2 * sizeof(int16_t)));
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            break;
        }
        offset += frames;
    }
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    esp_codec_dev_set_out_mute(s_codec, true);
    xSemaphoreGive(s_codec_lock);
    free(mono);
    free(stereo);
    fclose(file);
    ESP_LOGI(TAG, "WAV SD %s: %u/%u muestras, %u ms",
             cancelled ? "cancelado" : "reproducido", (unsigned)offset,
             (unsigned)count,
             (unsigned)(offset * 1000U / AUDIO_WAV_SAMPLE_RATE));
    return err;
}

esp_err_t audio_player_play_wav(const uint8_t *wav, size_t size)
{
    return play_wav(wav, size, 0, false);
}

static void audio_task(void *argument)
{
    (void)argument;
    audio_request_t request;
    while (true) {
        if (xQueueReceive(s_play_queue, &request, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = request.path[0]
                ? play_wav_file(request.path, request.generation)
                : play_wav(request.wav, request.size, request.generation, true);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Reproduccion asincrona: %s", esp_err_to_name(err));
            }
            free(request.wav);
        }
    }
}

void audio_player_stop(void)
{
    if (!s_play_queue) return;
    portENTER_CRITICAL(&s_generation_lock);
    ++s_play_generation;
    portEXIT_CRITICAL(&s_generation_lock);
    audio_request_t pending;
    while (xQueueReceive(s_play_queue, &pending, 0) == pdTRUE) {
        free(pending.wav);
    }
}

esp_err_t audio_player_play_wav_async(uint8_t *wav, size_t size)
{
    if (!s_ready || !s_play_queue || !wav) return ESP_ERR_INVALID_STATE;
    audio_player_stop();
    esp_err_t err = audio_player_validate_wav(wav, size);
    if (err != ESP_OK) return err;

    audio_request_t request = {
        .wav = wav,
        .size = size,
        .generation = s_play_generation,
    };
    return xQueueSend(s_play_queue, &request, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t audio_player_play_wav_file_async(const char *absolute_path)
{
    if (!s_ready || !s_play_queue || !absolute_path) return ESP_ERR_INVALID_STATE;
    size_t path_length = strlen(absolute_path);
    if (path_length == 0 || path_length >= AUDIO_FILE_PATH_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    long samples_offset = 0;
    size_t sample_count = 0;
    esp_err_t err = inspect_wav_file(absolute_path, NULL, &samples_offset,
                                     &sample_count);
    if (err != ESP_OK) return err;
    audio_player_stop();
    audio_request_t request = {
        .wav = NULL,
        .size = 0,
        .generation = s_play_generation,
    };
    memcpy(request.path, absolute_path, path_length + 1);
    return xQueueSend(s_play_queue, &request, 0) == pdTRUE
        ? ESP_OK : ESP_ERR_TIMEOUT;
}
