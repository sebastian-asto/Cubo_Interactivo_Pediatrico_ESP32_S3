idf_component_register(
    SRCS "es8311.c" "audio_player.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_manager i2s_manager esp_driver_gpio
)
