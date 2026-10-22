idf_component_register(
    SRCS
    "main.c"
    "app/power_button/pwr_button.c"
    "app/emotions/emotions.c"
    "app/ble/cube_ble.c"

    INCLUDE_DIRS "."
    "app/power_button"
    "app/emotions"
    "app/ble"

    REQUIRES st7789 microSD audio bt nvs_flash)

# Desde V1.5.0 las 16 caras de fabrica viven en
# /Doc_microSD/default/images. No se duplican dentro de la flash.

# La imagen se guarda en la flash junto con la aplicacion, sin montar SPIFFS.
# Reconfigurar despues de agregar o quitar assets/drawing.tft.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/drawing.tft")
    target_add_binary_data(${COMPONENT_LIB} "assets/drawing.tft" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HAS_DRAWING=1)
endif()

