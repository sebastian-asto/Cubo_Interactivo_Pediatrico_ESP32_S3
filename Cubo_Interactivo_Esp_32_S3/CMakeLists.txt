# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/componentes/driver/spi_manager"
    "${CMAKE_CURRENT_LIST_DIR}/componentes/modules/st7789"
    "${CMAKE_CURRENT_LIST_DIR}/componentes/modules/microSD"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Cubo_Interactivo_Esp_32_S3)
