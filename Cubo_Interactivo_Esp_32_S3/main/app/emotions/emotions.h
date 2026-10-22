#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

size_t emotions_count(void);
const char *emotions_name(size_t index);
/* Tarjeta ya montada. Copia solamente archivos ausentes. */
esp_err_t emotions_install(void);
/* Lee de microSD, valida TFT1 y muestra el dibujo. Libera el buffer al terminar. */
esp_err_t emotions_show(size_t index);
esp_err_t emotions_replace(size_t index, const uint8_t *data, size_t size);
esp_err_t emotions_restore(size_t index);
