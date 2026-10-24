#pragma once

#include "driver/sdmmc_host.h"
#include "esp_err.h"

/* SDMMC es independiente de SPI2 (TFT). Pull-ups externos de 10K a 3V3. */
#define BUS_SD_PIN_D2  GPIO_NUM_13
#define BUS_SD_PIN_D3  GPIO_NUM_14
#define BUS_SD_PIN_CMD GPIO_NUM_15
#define BUS_SD_PIN_CLK GPIO_NUM_16
#define BUS_SD_PIN_D0  GPIO_NUM_17
#define BUS_SD_PIN_D1  GPIO_NUM_18

/* Prepara la configuracion. El montaje FAT inicializa el host y el slot. */
esp_err_t bus_sdmmc_configure(sdmmc_host_t *host, sdmmc_slot_config_t *slot);
