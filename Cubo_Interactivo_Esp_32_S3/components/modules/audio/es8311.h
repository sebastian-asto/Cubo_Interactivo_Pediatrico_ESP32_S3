#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bus_i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES8311_I2C_ADDRESS 0x18

typedef struct es8311 *es8311_handle_t;

typedef struct {
    bus_i2c_handle_t i2c;
    gpio_num_t pa_gpio;
    bool pa_active_high;
    uint32_t sample_rate_hz;
    uint32_t mclk_hz;
} es8311_config_t;

esp_err_t es8311_init(const es8311_config_t *config, es8311_handle_t *handle);
esp_err_t es8311_set_volume(es8311_handle_t handle, uint8_t volume);
esp_err_t es8311_set_mute(es8311_handle_t handle, bool mute);
void es8311_deinit(es8311_handle_t handle);

#ifdef __cplusplus
}
#endif
