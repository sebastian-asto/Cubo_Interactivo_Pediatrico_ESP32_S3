#include "bus_spi.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "BUS_SPI";
static bool s_bus_initialized;

esp_err_t bus_spi_init(size_t max_transfer_sz)
{
    if (s_bus_initialized) {
        return ESP_OK;
    }

    if (max_transfer_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BUS_SPI_PIN_MOSI,
        .miso_io_num = BUS_SPI_PIN_MISO,
        .sclk_io_num = BUS_SPI_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = max_transfer_sz,
    };

    esp_err_t err = spi_bus_initialize(BUS_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo inicializar SPI: %s", esp_err_to_name(err));
        return err;
    }

    s_bus_initialized = true;
    ESP_LOGI(TAG, "SPI listo: MOSI=%d, SCLK=%d", BUS_SPI_PIN_MOSI, BUS_SPI_PIN_SCLK);
    return ESP_OK;
}

esp_err_t bus_spi_add_device(const spi_device_interface_config_t *device_config,
                             spi_device_handle_t *device_handle)
{
    if (!s_bus_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device_config == NULL || device_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return spi_bus_add_device(BUS_SPI_HOST, device_config, device_handle);
}
