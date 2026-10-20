#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG,"TEST_S3");
    vTaskDelay(pdMS_TO_TICKS(1000));

}
