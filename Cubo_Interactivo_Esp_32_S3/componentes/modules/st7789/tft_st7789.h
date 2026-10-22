#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TFT_WIDTH  240U
#define TFT_HEIGHT 240U

/** Colores RGB565. */
typedef enum {
    BLACK   = 0x0000,
    NAVY    = 0x000F,
    BLUE    = 0x001F,
    GREEN   = 0x07E0,
    CYAN    = 0x07FF,
    RED     = 0xF800,
    MAGENTA = 0xF81F,
    YELLOW  = 0xFFE0,
    WHITE   = 0xFFFF,
} tft_color_t;

/** Inicializa GPIO, SPI, controlador ST7789 y framebuffer. */
esp_err_t tft_init(void);

/** Rellena el framebuffer; la pantalla cambia al llamar tft_Show(). */
esp_err_t tft_fullScreen(tft_color_t color);

/**
 * Carga un archivo TFT1 exportado por Pixel Studio en el framebuffer.
 * Centra la imagen y rellena el exterior de negro. No envia al panel:
 * llamar tft_Show(). Formato: "TFT1", ancho y alto uint16 big-endian,
 * seguidos de RGB565 big-endian por filas. Tamano maximo: 240x240.
 */
esp_err_t tft_draw_image(const uint8_t *image, size_t image_size);

/** Valida TFT1 sin cambiar el framebuffer ni requerir inicializacion del LCD. */
esp_err_t tft_validate_image(const uint8_t *image, size_t image_size);

/** Envia el framebuffer completo al LCD. */
esp_err_t tft_Show(void);

/** Enciende o apaga el backlight. */
esp_err_t tft_backlight(bool enabled);

#ifdef __cplusplus
}
#endif
