#include "audio_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bus_i2c.h"
#include "bus_i2s.h"
#include "es8311.h"
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
#define AUDIO_MCLK_MULTIPLE I2S_MCLK_MULTIPLE_256
#define AUDIO_VOLUME 65
#define AUDIO_FILE_PATH_MAX 128
#define AUDIO_STREAM_FRAMES 512U

static const char *TAG = "AUDIO";
static bus_i2c_handle_t s_i2c;
static bus_i2s_handle_t s_i2s;
static es8311_handle_t s_codec;
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

    const bus_i2s_config_t i2s_config = {
        .port = AUDIO_I2S_PORT,
        .mclk_gpio = AUDIO_MCLK_GPIO,
        .bclk_gpio = AUDIO_BCLK_GPIO,
        .ws_gpio = AUDIO_LRCK_GPIO,
        .dout_gpio = AUDIO_DOUT_GPIO,
        .sample_rate_hz = AUDIO_WAV_SAMPLE_RATE,
        .mclk_multiple = AUDIO_MCLK_MULTIPLE,
    };
    esp_err_t err = bus_i2s_open(&i2s_config, &s_i2s);
    if (err != ESP_OK) return err;

    const bus_i2c_config_t i2c_config = {
        .port = AUDIO_I2C_PORT,
        .sda_gpio = AUDIO_I2C_SDA_GPIO,
        .scl_gpio = AUDIO_I2C_SCL_GPIO,
        .clock_hz = 100000,
        .device_address = ES8311_I2C_ADDRESS,
    };
    err = bus_i2c_open(&i2c_config, &s_i2c);
    if (err != ESP_OK) {
        bus_i2s_close(s_i2s);
        s_i2s = NULL;
        return err;
    }

    const es8311_config_t codec_config = {
        .i2c = s_i2c,
        .pa_gpio = AUDIO_PA_GPIO,
        .pa_active_high = true,
        .sample_rate_hz = AUDIO_WAV_SAMPLE_RATE,
        .mclk_hz = AUDIO_WAV_SAMPLE_RATE * 256U,
    };
    err = es8311_init(&codec_config, &s_codec);
    if (err == ESP_OK) err = es8311_set_volume(s_codec, s_volume);
    if (err != ESP_OK) {
        es8311_deinit(s_codec);
        s_codec = NULL;
        bus_i2c_close(s_i2c);
        s_i2c = NULL;
        bus_i2s_close(s_i2s);
        s_i2s = NULL;
        return err;
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
    esp_err_t result = es8311_set_volume(s_codec, volume);
    if (result == ESP_OK) s_volume = volume;
    xSemaphoreGive(s_codec_lock);
    if (result != ESP_OK) return result;
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
    esp_err_t codec_result = es8311_set_mute(s_codec, false);
    xSemaphoreGive(s_codec_lock);
    if (codec_result != ESP_OK) {
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
        size_t bytes_written = 0;
        esp_err_t result = bus_i2s_write(s_i2s, stereo,
                                         frames * 2 * sizeof(int16_t),
                                         &bytes_written);
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_OK || bytes_written != frames * 2 * sizeof(int16_t)) {
            err = ESP_FAIL;
            break;
        }
        offset += frames;
    }
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    es8311_set_mute(s_codec, true);
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
        esp_err_t result = es8311_set_mute(s_codec, false);
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_OK) err = result;
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
        size_t bytes_written = 0;
        esp_err_t result = bus_i2s_write(s_i2s, stereo,
                                         frames * 2 * sizeof(int16_t),
                                         &bytes_written);
        xSemaphoreGive(s_codec_lock);
        if (result != ESP_OK || bytes_written != frames * 2 * sizeof(int16_t)) {
            err = ESP_FAIL;
            break;
        }
        offset += frames;
    }
    xSemaphoreTake(s_codec_lock, portMAX_DELAY);
    es8311_set_mute(s_codec, true);
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
