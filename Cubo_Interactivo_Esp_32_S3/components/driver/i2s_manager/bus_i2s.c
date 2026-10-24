#include "bus_i2s.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"

struct bus_i2s {
    i2s_chan_handle_t tx;
};

esp_err_t bus_i2s_open(const bus_i2s_config_t *config, bus_i2s_handle_t *handle)
{
    if (!config || !handle || config->sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bus_i2s_handle_t context = calloc(1, sizeof(*context));
    if (!context) return ESP_ERR_NO_MEM;

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(config->port,
                                                            I2S_ROLE_MASTER);
    channel.auto_clear = true;
    esp_err_t err = i2s_new_channel(&channel, &context->tx, NULL);
    if (err != ESP_OK) {
        free(context);
        return err;
    }

    i2s_std_config_t standard = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = config->mclk_gpio,
            .bclk = config->bclk_gpio,
            .ws = config->ws_gpio,
            .dout = config->dout_gpio,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard.clk_cfg.mclk_multiple = config->mclk_multiple;
    err = i2s_channel_init_std_mode(context->tx, &standard);
    if (err == ESP_OK) err = i2s_channel_enable(context->tx);
    if (err != ESP_OK) {
        i2s_del_channel(context->tx);
        free(context);
        return err;
    }

    *handle = context;
    return ESP_OK;
}

esp_err_t bus_i2s_write(bus_i2s_handle_t handle, const void *data,
                        size_t byte_count, size_t *bytes_written)
{
    if (!handle || !data || byte_count == 0) return ESP_ERR_INVALID_ARG;
    return i2s_channel_write(handle->tx, data, byte_count, bytes_written,
                             portMAX_DELAY);
}

void bus_i2s_close(bus_i2s_handle_t handle)
{
    if (!handle) return;
    if (handle->tx) {
        i2s_channel_disable(handle->tx);
        i2s_del_channel(handle->tx);
    }
    free(handle);
}
