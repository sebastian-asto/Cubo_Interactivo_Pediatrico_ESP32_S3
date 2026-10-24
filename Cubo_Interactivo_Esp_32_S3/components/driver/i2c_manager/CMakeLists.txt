idf_component_register(
    SRCS "bus_i2c.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_gpio
)
