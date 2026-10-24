idf_component_register(
    SRCS "bus_i2s.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2s esp_driver_gpio
)
