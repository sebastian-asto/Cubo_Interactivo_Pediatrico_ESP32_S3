#pragma once

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bus SPI fisico al que esta conectado el LCD. */
#define BUS_SPI_HOST       SPI2_HOST
#define BUS_SPI_PIN_MOSI   GPIO_NUM_39
#define BUS_SPI_PIN_MISO   GPIO_NUM_NC
#define BUS_SPI_PIN_SCLK   GPIO_NUM_38

/** Inicializa una sola vez el bus SPI fisico. */
esp_err_t bus_spi_init(size_t max_transfer_sz);

/** Agrega un dispositivo al bus ya inicializado. */
esp_err_t bus_spi_add_device(const spi_device_interface_config_t *device_config,
                             spi_device_handle_t *device_handle);

#ifdef __cplusplus
}
#endif
