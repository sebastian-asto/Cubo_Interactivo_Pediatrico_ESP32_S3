#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "tft_st7789.h"

static const char* TAG = "MAIN";

static tft_color_t colors[4] = {RED,BLUE,YELLOW,WHITE};

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando pantalla ST7789");
    ESP_ERROR_CHECK(tft_init());

    while(1){
        for(int i = 0; i < 4; i++){
            ESP_ERROR_CHECK(tft_fullScreen(colors[i]));
            ESP_ERROR_CHECK(tft_Show());
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

