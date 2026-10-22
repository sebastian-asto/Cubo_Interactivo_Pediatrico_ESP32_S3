#include "emotions.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "data_sd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tft_st7789.h"
#include "mbedtls/sha256.h"

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

#define DECLARE_IMAGE(id, label) \
    extern const uint8_t id##_start[] asm("_binary_" #id "_tft_start"); \
    extern const uint8_t id##_end[] asm("_binary_" #id "_tft_end");
EMOTION_LIST(DECLARE_IMAGE)
#undef DECLARE_IMAGE

typedef struct {
    const char *path;
    const char *label;
    const uint8_t *start;
    const uint8_t *end;
} emotion_asset_t;

#define IMAGE_ENTRY(id, label) {"emotions/" #id ".tft", label, id##_start, id##_end},
static const emotion_asset_t s_emotions[] = { EMOTION_LIST(IMAGE_ENTRY) };
#undef IMAGE_ENTRY
static const char *TAG = "EMOTIONS";

/* Solo migrar ilustraciones conocidas de fabrica; conservar dibujos personales. */
static bool previous_factory_version(const emotion_asset_t *asset,
                                   const uint8_t *image, size_t size)
{
    static const struct { const char *path; const char *sha256; } history[] = {
#include "factory_history.inc"
    };
    if (size != 115208 || (size == (size_t)(asset->end - asset->start) &&
                           memcmp(image, asset->start, size) == 0)) {
        return false;
    }
    uint8_t digest[32];
    if (mbedtls_sha256(image, size, digest, 0) != 0) {
        return false;
    }
    char hex[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    for (size_t i = 0; i < sizeof(history) / sizeof(history[0]); ++i) {
        if (strcmp(asset->path, history[i].path) == 0 &&
            strcmp(hex, history[i].sha256) == 0) {
            return true;
        }
    }
    return false;
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
    esp_err_t err = data_sd_mkdir("emotions");
    if (err != ESP_OK) {
        return err;
    }
    esp_err_t first_error = ESP_OK;
    unsigned created_count = 0;
    unsigned preserved_count = 0;
    for (size_t i = 0; i < emotions_count(); ++i) {
        const emotion_asset_t *asset = &s_emotions[i];
        err = data_sd_recover(asset->path);
        if (err != ESP_OK) return err;
        bool created = false;
        uint8_t *existing = NULL;
        size_t existing_size = 0;
        err = data_sd_read_file(asset->path,
            8U + TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t), &existing, &existing_size);
        if (err == ESP_ERR_NOT_FOUND) {
            err = ESP_OK;
        } else if (err == ESP_OK && tft_validate_image(existing, existing_size) != ESP_OK) {
            err = data_sd_backup_file(asset->path);
        } else if (err == ESP_OK && previous_factory_version(asset, existing, existing_size)) {
            err = data_sd_replace_factory_file(asset->path, asset->start,
                                               asset->end - asset->start, ".v2");
        }
        free(existing);
        if (err == ESP_OK) {
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                err = data_sd_write_if_absent(asset->path, asset->start,
                                             asset->end - asset->start, &created);
                if (err == ESP_OK) {
                    break;
                }
                ESP_LOGW(TAG, "Escritura %s, intento %u: %s", asset->path,
                         attempt + 1, esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "No se pudo instalar %s: %s", asset->path, esp_err_to_name(err));
            if (first_error == ESP_OK) {
                first_error = err;
            }
        } else {
            if (created) {
                ++created_count;
            } else {
                ++preserved_count;
            }
            ESP_LOGI(TAG, "%s: %s", created ? "Copiado a SD" : "Conservado en SD", asset->path);
        }
    }
    ESP_LOGI(TAG, "SD_IMAGENES: %u nuevas, %u existentes, %u esperadas",
             created_count, preserved_count, (unsigned)emotions_count());
    return first_error;
}

esp_err_t emotions_show(size_t index)
{
    if (index >= emotions_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *image = NULL;
    size_t size = 0;
    esp_err_t err = data_sd_read_file(s_emotions[index].path,
        8U + TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t), &image, &size);
    if (err == ESP_OK) {
        err = tft_draw_image(image, size);
        if (err == ESP_OK &&
            size == (size_t)(s_emotions[index].end - s_emotions[index].start) &&
            memcmp(image, s_emotions[index].start, size) == 0) {
            ESP_LOGI(TAG, "SD_VERSION_ACTUAL: %s", s_emotions[index].path);
        }
        if (err != ESP_OK && size >= 8) {
            ESP_LOGE(TAG, "TFT1 SD: bytes=%u cabecera=%02x %02x %02x %02x %02x %02x %02x %02x",
                     (unsigned)size, image[0], image[1], image[2], image[3],
                     image[4], image[5], image[6], image[7]);
            const uint8_t *expected = s_emotions[index].start;
            ESP_LOGE(TAG, "TFT1 flash: bytes=%u cabecera=%02x %02x %02x %02x %02x %02x %02x %02x",
                     (unsigned)(s_emotions[index].end - expected), expected[0], expected[1],
                     expected[2], expected[3], expected[4], expected[5], expected[6], expected[7]);
        }
    }
    free(image);
    if (err == ESP_OK) {
        err = tft_Show();
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD_MOSTRANDO %u/%u: %s", (unsigned)index + 1,
                 (unsigned)emotions_count(), s_emotions[index].label);
    } else {
        ESP_LOGE(TAG, "%s: %s", s_emotions[index].path, esp_err_to_name(err));
    }
    return err;
}

esp_err_t emotions_replace(size_t index, const uint8_t *data, size_t size)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    esp_err_t err = tft_validate_image(data, size);
    if (err != ESP_OK) return err;
    return data_sd_replace(s_emotions[index].path, data, size);
}

esp_err_t emotions_restore(size_t index)
{
    if (index >= emotions_count()) return ESP_ERR_INVALID_ARG;
    const emotion_asset_t *a = &s_emotions[index];
    uint8_t *existing = NULL;
    size_t size = 0;
    esp_err_t err = data_sd_read_file(a->path, 115208, &existing, &size);
    bool same = err == ESP_OK && size == (size_t)(a->end - a->start) &&
                memcmp(existing, a->start, size) == 0;
    free(existing);
    if (same) return ESP_OK;
    return emotions_replace(index, a->start, a->end - a->start);
}
