#include "pwr_button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PWR_BUTTON_GPIO GPIO_NUM_5
#define PWR_OUTPUT_GPIO GPIO_NUM_2
#define PWR_PRESS_VALID_MS 50
#define PWR_RELEASE_POLL_MS 10
#define PWR_RELEASE_DEBOUNCE_MS 20

static const char *TAG = "PWR_BUTTON";
static TaskHandle_t s_button_task;
static volatile bool s_power_hold;

static void button_isr(void *argument)
{
    (void)argument;
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_button_task, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) portYIELD_FROM_ISR();
}

static void button_task(void *argument)
{
    (void)argument;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Confirma que no fue ruido ni rebote corto en el flanco de bajada. */
        vTaskDelay(pdMS_TO_TICKS(PWR_PRESS_VALID_MS));
        if (gpio_get_level(PWR_BUTTON_GPIO) != 0) {
            continue;
        }

        /*
         * El pulsador mantiene Q2 conduciendo mientras permanece presionado.
         * BAT_EN se libera ahora y la alimentación por batería desaparece al
         * soltarlo. El siguiente encendido vuelve a ejecutar init y enclava
         * BAT_EN en HIGH.
         */
        esp_err_t err = gpio_set_level(PWR_OUTPUT_GPIO, 0);
        if (err == ESP_OK) {
            s_power_hold = false;
            ESP_LOGI(TAG,
                     "Apagado solicitado: BAT_EN/GPIO2=LOW; suelta el boton");
        } else {
            ESP_LOGE(TAG, "No se pudo actualizar GPIO2: %s",
                     esp_err_to_name(err));
        }

        /* Mantiene viva la tarea si VBUS impide que el devkit se apague. */
        while (gpio_get_level(PWR_BUTTON_GPIO) == 0) {
            vTaskDelay(pdMS_TO_TICKS(PWR_RELEASE_POLL_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(PWR_RELEASE_DEBOUNCE_MS));
        ulTaskNotifyTake(pdTRUE, 0);

        if (!s_power_hold) {
            ESP_LOGW(TAG,
                     "BAT_EN sigue LOW; con USB/VBUS el devkit puede continuar encendido");
            gpio_intr_disable(PWR_BUTTON_GPIO);
            vTaskSuspend(NULL);
        }
    }
}

esp_err_t pwr_button_init(void)
{
    if (s_button_task) return ESP_ERR_INVALID_STATE;

    const gpio_config_t output_config = {
        .pin_bit_mask = 1ULL << PWR_OUTPUT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) return err;
    /* Enclava Q2 mediante T1 antes de que el usuario suelte KEY_PWR. */
    err = gpio_set_level(PWR_OUTPUT_GPIO, 1);
    if (err != ESP_OK) return err;
    s_power_hold = true;

    const gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << PWR_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        /* La placa ya incorpora el pull-up externo de 10 kOhm a 3.3 V. */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&input_config);
    if (err != ESP_OK) return err;

    if (xTaskCreate(button_task, "power_button", 2048, NULL, 5,
                    &s_button_task) != pdPASS) {
        s_button_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        vTaskDelete(s_button_task);
        s_button_task = NULL;
        return err;
    }
    err = gpio_isr_handler_add(PWR_BUTTON_GPIO, button_isr, NULL);
    if (err != ESP_OK) {
        vTaskDelete(s_button_task);
        s_button_task = NULL;
        return err;
    }

    ESP_LOGI(TAG,
             "Energia enclavada: KEY_PWR/GPIO5 activo LOW; BAT_EN/GPIO2=HIGH");
    return ESP_OK;
}

bool pwr_button_power_is_held(void)
{
    return s_power_hold;
}
