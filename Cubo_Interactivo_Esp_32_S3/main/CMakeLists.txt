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

    REQUIRES st7789 microSD mbedtls bt nvs_flash)

# Semillas de fabrica: el primer arranque las copia a la microSD si faltan.
file(GLOB EMOTION_IMAGES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/emotions/*.tft")
foreach(EMOTION_IMAGE IN LISTS EMOTION_IMAGES)
    target_add_binary_data(${COMPONENT_LIB} "${EMOTION_IMAGE}" BINARY)
endforeach()

# La imagen se guarda en la flash junto con la aplicacion, sin montar SPIFFS.
# Reconfigurar despues de agregar o quitar assets/drawing.tft.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/assets/drawing.tft")
    target_add_binary_data(${COMPONENT_LIB} "assets/drawing.tft" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_HAS_DRAWING=1)
endif()

