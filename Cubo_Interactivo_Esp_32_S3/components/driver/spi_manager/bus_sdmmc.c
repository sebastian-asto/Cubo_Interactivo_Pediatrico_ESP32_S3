#include "bus_sdmmc.h"

esp_err_t bus_sdmmc_configure(sdmmc_host_t *host, sdmmc_slot_config_t *slot)
{
    if (host == NULL || slot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const sdmmc_host_t default_host = SDMMC_HOST_DEFAULT();
    const sdmmc_slot_config_t default_slot = SDMMC_SLOT_CONFIG_DEFAULT();
    *host = default_host;
    *slot = default_slot;
    host->max_freq_khz = 10000; /* 10 MHz; margen para la tarjeta SDSC de la placa. */
    host->flags &= ~(SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR);
    slot->width = 4;
    slot->clk = BUS_SD_PIN_CLK;
    slot->cmd = BUS_SD_PIN_CMD;
    slot->d0 = BUS_SD_PIN_D0;
    slot->d1 = BUS_SD_PIN_D1;
    slot->d2 = BUS_SD_PIN_D2;
    slot->d3 = BUS_SD_PIN_D3;
    /* La placa ya tiene pull-ups externos; no se depende de los internos. */
    return ESP_OK;
}
