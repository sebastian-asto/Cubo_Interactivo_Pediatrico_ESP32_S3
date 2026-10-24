#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configura KEY_PWR/GPIO5 como botón activo en LOW y BAT_EN/GPIO2 como
 * retención de alimentación. GPIO2 pasa a HIGH al arrancar; una pulsación
 * válida de al menos 50 ms lo lleva a LOW para apagar al soltar el botón.
 */
esp_err_t pwr_button_init(void);

/** Indica si BAT_EN/GPIO2 mantiene habilitada la alimentación por batería. */
bool pwr_button_power_is_held(void);

#ifdef __cplusplus
}
#endif
