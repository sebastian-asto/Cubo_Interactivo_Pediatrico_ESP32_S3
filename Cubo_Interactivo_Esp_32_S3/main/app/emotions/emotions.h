#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

size_t emotions_count(void);
const char *emotions_name(size_t index);
/* Tarjeta ya montada. Valida Doc_microSD y prepara directorios de usuario. */
esp_err_t emotions_install(void);
/* Prioriza user/images y usa default/images si no existe una personalizacion. */
esp_err_t emotions_show(size_t index);
/* Comprueba exclusivamente la imagen de usuario antes de mostrarla. */
esp_err_t emotions_show_if_crc(size_t index, size_t expected_size,
                               uint32_t expected_crc);
esp_err_t emotions_replace(size_t index, const uint8_t *data, size_t size);
/* Exporta solo una imagen de usuario. Si coincide con size/crc devuelve OK,
 * data=NULL y size=0; si difiere transfiere al llamador el buffer asignado. */
esp_err_t emotions_export_image(size_t index, size_t known_size,
                                uint32_t known_crc, uint8_t **data,
                                size_t *size);
void emotions_resource_masks(uint16_t *image_mask, uint16_t *audio_mask);
/* Elimina solo la imagen de usuario; el original permanece intacto. */
esp_err_t emotions_restore(size_t index);
esp_err_t emotions_audio_replace(size_t index, const uint8_t *data, size_t size);
esp_err_t emotions_audio_matches(size_t index, size_t expected_size,
                                 uint32_t expected_crc);
esp_err_t emotions_audio_restore(size_t index);
esp_err_t emotions_play_audio(size_t index);
