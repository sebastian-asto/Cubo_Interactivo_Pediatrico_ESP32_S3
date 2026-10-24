#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bus_i2s *bus_i2s_handle_t;

typedef struct {
    i2s_port_t port;
    gpio_num_t mclk_gpio;
    gpio_num_t bclk_gpio;
    gpio_num_t ws_gpio;
    gpio_num_t dout_gpio;
    uint32_t sample_rate_hz;
    i2s_mclk_multiple_t mclk_multiple;
} bus_i2s_config_t;

esp_err_t bus_i2s_open(const bus_i2s_config_t *config, bus_i2s_handle_t *handle);
esp_err_t bus_i2s_write(bus_i2s_handle_t handle, const void *data,
                        size_t byte_count, size_t *bytes_written);
void bus_i2s_close(bus_i2s_handle_t handle);

#ifdef __cplusplus
}
#endif
