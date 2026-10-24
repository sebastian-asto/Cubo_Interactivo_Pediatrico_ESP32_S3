#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bus_i2c_device *bus_i2c_handle_t;

typedef struct {
    i2c_port_num_t port;
    gpio_num_t sda_gpio;
    gpio_num_t scl_gpio;
    uint32_t clock_hz;
    uint16_t device_address;
} bus_i2c_config_t;

esp_err_t bus_i2c_open(const bus_i2c_config_t *config, bus_i2c_handle_t *handle);
esp_err_t bus_i2c_write_reg(bus_i2c_handle_t handle, uint8_t reg, uint8_t value);
esp_err_t bus_i2c_read_reg(bus_i2c_handle_t handle, uint8_t reg, uint8_t *value);
void bus_i2c_close(bus_i2c_handle_t handle);

#ifdef __cplusplus
}
#endif
