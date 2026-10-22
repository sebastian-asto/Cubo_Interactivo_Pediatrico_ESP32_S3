#include "esp_log.h"
#include "tft_st7789.h"
#include "data_sd.h"
#include "emotions.h"
#include "cube_ble.h"
#ifdef APP_HAS_DRAWING
extern const uint8_t drawing_start[] asm("_binary_drawing_tft_start");
extern const uint8_t drawing_end[] asm("_binary_drawing_tft_end");
#endif
void app_main(void)
{
    ESP_ERROR_CHECK(tft_init());
    bool sd_ready = data_sd_mount() == ESP_OK;
    esp_err_t shown = ESP_FAIL;
    if (sd_ready) {
        esp_err_t err = emotions_install();
        ESP_LOGI("MAIN", "Instalacion SD: %s", esp_err_to_name(err));
        shown = emotions_show(0);
    }
#ifdef APP_HAS_DRAWING
    if (shown != ESP_OK && tft_draw_image(drawing_start, drawing_end-drawing_start) == ESP_OK) {
        ESP_ERROR_CHECK(tft_Show());
    }
#endif
    /* From here only cube_commands owns the display and SD. Keep selected face. */
    ESP_ERROR_CHECK(cube_ble_start(sd_ready));
}
