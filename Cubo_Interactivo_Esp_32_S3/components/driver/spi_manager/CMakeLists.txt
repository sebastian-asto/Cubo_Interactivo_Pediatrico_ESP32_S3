idf_component_register(SRCS "bus_spi.c" "bus_sdmmc.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_sdmmc sdmmc)
