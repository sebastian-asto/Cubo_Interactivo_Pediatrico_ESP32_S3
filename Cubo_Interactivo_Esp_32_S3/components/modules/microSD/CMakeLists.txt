idf_component_register(SRCS "data_sd.c"
                       INCLUDE_DIRS "."
                       REQUIRES spi_manager fatfs sdmmc)
