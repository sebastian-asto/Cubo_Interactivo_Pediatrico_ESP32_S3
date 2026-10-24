#include "bus_i2c.h"

#include <stdlib.h>

struct bus_i2c_device {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
};

esp_err_t bus_i2c_open(const bus_i2c_config_t *config, bus_i2c_handle_t *handle)
{
    if (!config || !handle || config->clock_hz == 0) return ESP_ERR_INVALID_ARG;

    bus_i2c_handle_t context = calloc(1, sizeof(*context));
    if (!context) return ESP_ERR_NO_MEM;

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_gpio,
        .scl_io_num = config->scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &context->bus);
    if (err != ESP_OK) {
        free(context);
        return err;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->device_address,
        .scl_speed_hz = config->clock_hz,
    };
    err = i2c_master_bus_add_device(context->bus, &device_config,
                                    &context->device);
    if (err != ESP_OK) {
        i2c_del_master_bus(context->bus);
        free(context);
        return err;
    }

    *handle = context;
    return ESP_OK;
}

esp_err_t bus_i2c_write_reg(bus_i2c_handle_t handle, uint8_t reg, uint8_t value)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    const uint8_t payload[] = {reg, value};
    return i2c_master_transmit(handle->device, payload, sizeof(payload), 100);
}

esp_err_t bus_i2c_read_reg(bus_i2c_handle_t handle, uint8_t reg, uint8_t *value)
{
    if (!handle || !value) return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(handle->device, &reg, 1, value, 1, 100);
}

void bus_i2c_close(bus_i2c_handle_t handle)
{
    if (!handle) return;
    if (handle->device) i2c_master_bus_rm_device(handle->device);
    if (handle->bus) i2c_del_master_bus(handle->bus);
    free(handle);
}
