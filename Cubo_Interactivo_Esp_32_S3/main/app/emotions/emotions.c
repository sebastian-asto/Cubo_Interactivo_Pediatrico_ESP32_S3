#include "emotions.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "audio_player.h"
#include "data_sd.h"
#include "esp_log.h"
#include "tft_st7789.h"

#define EMOTION_LIST(X) \
    X(joy, "Estoy feliz") \
    X(sad, "Estoy triste") \
    X(angry, "Estoy enfadado") \
    X(upset, "Estoy molesto") \
    X(serious, "Estoy serio") \
    X(sorry, "Estoy avergonzado") \
    X(excited, "Estoy emocionado") \
    X(inlove, "Estoy enamorado") \
    X(nervous, "Estoy nervioso") \
    X(thought, "Estoy pensativo") \
    X(bored, "Estoy aburrido") \
    X(surprise, "Estoy sorprendido") \
    X(worried, "Estoy preocupado") \
    X(scared, "Estoy asustado") \
    X(crying, "Estoy deprimido") \
    X(proud, "Estoy orgulloso")

typedef struct {
    const char *id;
    const char *label;
} emotion_asset_t;

#define EMOTION_ENTRY(id, label) {#id, label},
static const emotion_asset_t s_emotions[] = {EMOTION_LIST(EMOTION_ENTRY)};
#undef EMOTION_ENTRY

static const char *TAG = "EMOTIONS";

static esp_err_t resource_path(char *path, size_t capacity, const char *area,
                               size_t index, const char *extension)
{
    if (!path || !area || !extension || index >= emotions_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    int count = snprintf(path, capacity, "Doc_microSD/%s/%s.%s",
                         area, s_emotions[index].id, extension);
    return count > 0 && (size_t)count < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static uint32_t image_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffff;
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
    }
    return ~crc;
}

size_t emotions_count(void)
{
    return sizeof(s_emotions) / sizeof(s_emotions[0]);
}

const char *emotions_name(size_t index)
{
    return index < emotions_count() ? s_emotions[index].label : "Indice invalido";
}

esp_err_t emotions_install(void)
{
    static const char *directories[] = {
        "Doc_microSD",
        "Doc_microSD/default",
        "Doc_microSD/default/images",
        "Doc_microSD/user",
        "Doc_microSD/user/images",
        "Doc_microSD/user/audio",
    };
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); ++i) {
        esp_err_t err = data_sd_mkdir(directories[i]);
        if (err != ESP_OK) return err;
    }

    esp_err_t first_error = ESP_OK;
    unsigned valid = 0;
    for (size_t i = 0; i < emotions_count(); ++i) {
        char path[96];
        resource_path(path, sizeof(path), "default/images", i, "tft");
        uint8_t *image = NULL;
        size_t size = 0;
        esp_err_t err = data_sd_read_file(path, 115208, &image, &size);
        if (err == ESP_OK) err = tft_validate_image(image, size);
        free(image);
        if (err == ESP_OK) {
            ++valid;
        } else {
            ESP_LOGE(TAG, "Recurso predeterminado invalido o ausente: %s (%s)",
                     path, esp_err_to_name(err));
            if (first_error == ESP_OK) first_error = err;
        }

        resource_path(path, sizeof(path), "user/images", i, "tft");
        esp_err_t recover = data_sd_recover(path);
        if (recover != ESP_OK && first_error == ESP_OK) first_error = recover;
        resource_path(path, sizeof(path), "user/audio", i, "wav");
        recover = data_sd_recover(path);
        if (recover != ESP_OK && first_error == ESP_OK) first_error = recover;
    }
    ESP_LOGI(TAG, "SD_RECURSOS: %u/%u caras predeterminadas validas",
             valid, (unsigned)emotions_count());
    return first_error;
}

static esp_err_t read_image(size_t index, bool user_only,
                            uint8_t **image, size_t *size, bool *custom)
{
    if (index >= emotions_count() || !image || !size) return ESP_ERR_INVALID_ARG;
    char path[96];
    resource_path(path, sizeof(path), "user/images", index, "tft");
    esp_err_t err = data_sd_read_file(path, 115208, image, size);
    if (err == ESP_OK && tft_validate_image(*image, *size) == ESP_OK) {
        if (custom) *custom = true;
        return ESP_OK;
    }
    free(*image);
    *image = NULL;
    *size = 0;
    if (user_only) return err == ESP_OK ? ESP_ERR_INVALID_ARG : err;

    resource_path(path, sizeof(path), "default/images", index, "tft");
    err = data_sd_read_file(path, 115208, image, size);
    if (err == ESP_OK) err = tft_validate_image(*image, *size);
    if (err != ESP_OK) {
        free(*image);
        *image = NULL;
        *size = 0;
    } else if (custom) {
        *custom = false;
    }
    return err;
}

static esp_err_t show_internal(size_t index, bool check_crc, uint32_t expected_crc)
{
    uint8_t *image = NULL;
    size_t size = 0;
    bool custom = false;
    esp_err_t err = read_image(index, check_crc, &image, &size, &custom);
    if (err == ESP_OK && check_crc && image_crc32(image, size) != expected_crc) {
        err = ESP_ERR_INVALID_CRC;
    }
    if (err == ESP_OK) err = tft_draw_image(image, size);
    free(image);
    if (err == ESP_OK) err = tft_Show();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD_MOSTRANDO %u/%u: %s (%s)", (unsigned)index + 1,
                 (unsigned)emotions_count(), s_emotions[index].label,
                 custom ? "usuario" : "default");
    }
    return err;
}

esp_err_t emotions_show(size_t index)
{
    return show_internal(index, false, 0);
}

esp_err_t emotions_show_if_crc(size_t index, uint32_t expected_crc)
{
    return show_internal(index, true, expected_crc);
}

esp_err_t emotions_replace(size_t index, const uint8_t *data, size_t size)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    esp_err_t err = tft_validate_image(data, size);
    if (err != ESP_OK) return err;
    char path[96];
    resource_path(path, sizeof(path), "user/images", index, "tft");
    return data_sd_replace(path, data, size);
}

esp_err_t emotions_restore(size_t index)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    char path[96];
    resource_path(path, sizeof(path), "user/images", index, "tft");
    return data_sd_delete_file(path);
}

esp_err_t emotions_audio_replace(size_t index, const uint8_t *data, size_t size)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    esp_err_t err = audio_player_validate_wav(data, size);
    if (err != ESP_OK) return err;
    char path[96];
    resource_path(path, sizeof(path), "user/audio", index, "wav");
    return data_sd_replace(path, data, size);
}

esp_err_t emotions_audio_matches(size_t index, uint32_t expected_crc)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    char path[96];
    resource_path(path, sizeof(path), "user/audio", index, "wav");
    uint8_t *wav = NULL;
    size_t size = 0;
    esp_err_t err = data_sd_read_file(path, AUDIO_WAV_MAX_BYTES, &wav, &size);
    if (err == ESP_OK && audio_player_validate_wav(wav, size) != ESP_OK) {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && image_crc32(wav, size) != expected_crc) {
        err = ESP_ERR_INVALID_CRC;
    }
    free(wav);
    return err;
}

esp_err_t emotions_audio_restore(size_t index)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    char path[96];
    resource_path(path, sizeof(path), "user/audio", index, "wav");
    return data_sd_delete_file(path);
}

esp_err_t emotions_play_audio(size_t index)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    char path[96];
    resource_path(path, sizeof(path), "user/audio", index, "wav");
    uint8_t *wav = NULL;
    size_t size = 0;
    esp_err_t err = data_sd_read_file(path, AUDIO_WAV_MAX_BYTES, &wav, &size);
    if (err == ESP_OK) err = audio_player_play_wav(wav, size);
    free(wav);
    return err;
}
