#include "tft_st7789.h"

#include <stdbool.h>
#include <string.h>

#include "bus_spi.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TFT_PIN_CS          GPIO_NUM_21
#define TFT_PIN_RST         GPIO_NUM_40
#define TFT_PIN_DC          GPIO_NUM_45
#define TFT_PIN_BL          GPIO_NUM_46
#define TFT_SPI_CLOCK_HZ    (70 * 1000 * 1000)
#define TFT_TRANSFER_LINES  20U
#define TFT_TRANSFER_BYTES  (TFT_WIDTH * TFT_TRANSFER_LINES * sizeof(uint16_t))
#define TFT_FRAME_BYTES     (TFT_WIDTH * TFT_HEIGHT * sizeof(uint16_t))

/* El panel de esta placa empieza en (0, 0) dentro de la RAM del ST7789. */
#define TFT_X_OFFSET 0U
#define TFT_Y_OFFSET 0U

static const char *TAG = "ST7789";
static spi_device_handle_t s_spi;
static uint16_t *s_framebuffer;
static uint8_t *s_dma_buffer;
static bool s_initialized;

typedef struct {
    uint8_t command;
    uint8_t data[16];
    uint8_t data_length;
    uint16_t delay_ms;
} st7789_init_command_t;

static esp_err_t tft_transmit(const void *data, size_t length)
{
    if (data == NULL || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_spi, &transaction);
}

static esp_err_t tft_write_command(uint8_t command)
{
    gpio_set_level(TFT_PIN_DC, 0);
    return tft_transmit(&command, sizeof(command));
}

static esp_err_t tft_write_data(const void *data, size_t length)
{
    gpio_set_level(TFT_PIN_DC, 1);
    return tft_transmit(data, length);
}

static esp_err_t tft_command_with_data(uint8_t command, const uint8_t *data, size_t length)
{
    esp_err_t err = tft_write_command(command);
    if (err != ESP_OK || length == 0) {
        return err;
    }
    return tft_write_data(data, length);
}

static esp_err_t tft_hardware_reset(void)
{
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t tft_controller_init(void)
{
    static const st7789_init_command_t init_commands[] = {
        {0x01, {0}, 0, 150},                         /* Software reset */
        {0x11, {0}, 0, 120},                         /* Sleep out */
        {0x36, {0x00}, 1, 0},                        /* RGB, portrait */
        {0x3A, {0x55}, 1, 0},                        /* RGB565 */
        {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},
        {0xB7, {0x35}, 1, 0},
        {0xBB, {0x19}, 1, 0},
        {0xC0, {0x2C}, 1, 0},
        {0xC2, {0x01}, 1, 0},
        {0xC3, {0x12}, 1, 0},
        {0xC4, {0x20}, 1, 0},
        {0xC6, {0x0F}, 1, 0},
        {0xD0, {0xA4, 0xA1}, 2, 0},
        {0xE0, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
                0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14, 0},
        {0xE1, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
                0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14, 0},
        {0x21, {0}, 0, 0},                           /* Display inversion on */
        {0x13, {0}, 0, 10},                          /* Normal display mode */
        {0x29, {0}, 0, 20},                          /* Display on */
    };

    for (size_t i = 0; i < sizeof(init_commands) / sizeof(init_commands[0]); ++i) {
        const st7789_init_command_t *item = &init_commands[i];
        esp_err_t err = tft_command_with_data(item->command, item->data, item->data_length);
        if (err != ESP_OK) {
            return err;
        }
        if (item->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(item->delay_ms));
        }
    }
    return ESP_OK;
}

static esp_err_t tft_set_window(uint16_t x_start, uint16_t y_start,
                                uint16_t x_end, uint16_t y_end)
{
    x_start += TFT_X_OFFSET;
    x_end += TFT_X_OFFSET;
    y_start += TFT_Y_OFFSET;
    y_end += TFT_Y_OFFSET;

    const uint8_t columns[] = {
        (uint8_t)(x_start >> 8), (uint8_t)x_start,
        (uint8_t)(x_end >> 8), (uint8_t)x_end,
    };
    const uint8_t rows[] = {
        (uint8_t)(y_start >> 8), (uint8_t)y_start,
        (uint8_t)(y_end >> 8), (uint8_t)y_end,
    };

    esp_err_t err = tft_command_with_data(0x2A, columns, sizeof(columns));
    if (err != ESP_OK) {
        return err;
    }
    err = tft_command_with_data(0x2B, rows, sizeof(rows));
    if (err != ESP_OK) {
        return err;
    }
    return tft_write_command(0x2C);
}

esp_err_t tft_backlight(bool enabled)
{
    return gpio_set_level(TFT_PIN_BL, enabled ? 1 : 0);
}

esp_err_t tft_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << TFT_PIN_RST) | (1ULL << TFT_PIN_DC) | (1ULL << TFT_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }
    tft_backlight(false);

    s_framebuffer = heap_caps_malloc(TFT_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_framebuffer == NULL) {
        ESP_LOGW(TAG, "PSRAM no disponible; usando RAM interna para el framebuffer");
        s_framebuffer = heap_caps_malloc(TFT_FRAME_BYTES, MALLOC_CAP_8BIT);
    }
    s_dma_buffer = heap_caps_malloc(TFT_TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_framebuffer == NULL || s_dma_buffer == NULL) {
        ESP_LOGE(TAG, "No hay memoria para los buffers del LCD");
        return ESP_ERR_NO_MEM;
    }

    err = bus_spi_init(TFT_TRANSFER_BYTES);
    if (err != ESP_OK) {
        return err;
    }

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = TFT_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = TFT_PIN_CS,
        .queue_size = 1,
    };
    err = bus_spi_add_device(&device_config, &s_spi);
    if (err != ESP_OK) {
        return err;
    }

    err = tft_hardware_reset();
    if (err != ESP_OK) {
        return err;
    }
    err = tft_controller_init();
    if (err != ESP_OK) {
        return err;
    }

    s_initialized = true;
    err = tft_fullScreen(BLACK);
    if (err == ESP_OK) {
        err = tft_Show();
    }
    if (err == ESP_OK) {
        err = tft_backlight(true);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "LCD ST7789 listo: %ux%u, SPI=%d MHz", TFT_WIDTH, TFT_HEIGHT,
                 TFT_SPI_CLOCK_HZ / 1000000);
    }
    return err;
}

esp_err_t tft_fullScreen(tft_color_t color)
{
    if (!s_initialized || s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* El ST7789 recibe primero el byte mas significativo de cada RGB565. */
    const uint16_t color_be = (uint16_t)(((uint16_t)color << 8) | ((uint16_t)color >> 8));
    const size_t pixel_count = TFT_WIDTH * TFT_HEIGHT;
    for (size_t i = 0; i < pixel_count; ++i) {
        s_framebuffer[i] = color_be;
    }
    return ESP_OK;
}

esp_err_t tft_Show(void)
{
    if (!s_initialized || s_framebuffer == NULL || s_dma_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(TFT_PIN_DC, 1);
    const uint8_t *source = (const uint8_t *)s_framebuffer;
    size_t remaining = TFT_FRAME_BYTES;
    while (remaining > 0) {
        const size_t chunk_size = remaining > TFT_TRANSFER_BYTES ? TFT_TRANSFER_BYTES : remaining;
        memcpy(s_dma_buffer, source, chunk_size);
        err = tft_transmit(s_dma_buffer, chunk_size);
        if (err != ESP_OK) {
            return err;
        }
        source += chunk_size;
        remaining -= chunk_size;
    }
    return ESP_OK;
}
