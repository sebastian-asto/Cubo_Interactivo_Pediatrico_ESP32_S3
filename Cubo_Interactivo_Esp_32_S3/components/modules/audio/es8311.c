/*
 * ES8311 DAC driver for the fixed 16 kHz Cubo Interactivo audio path.
 * Register sequence adapted from Espressif esp_codec_dev (Apache-2.0).
 * SPDX-License-Identifier: Apache-2.0
 */
#include "es8311.h"

#include <stdlib.h>
#include "esp_log.h"

#define REG_RESET       0x00
#define REG_CLK01       0x01
#define REG_CLK02       0x02
#define REG_CLK03       0x03
#define REG_CLK04       0x04
#define REG_CLK05       0x05
#define REG_CLK06       0x06
#define REG_CLK07       0x07
#define REG_CLK08       0x08
#define REG_SDP_IN      0x09
#define REG_SDP_OUT     0x0A
#define REG_SYSTEM_0B   0x0B
#define REG_SYSTEM_0C   0x0C
#define REG_SYSTEM_0D   0x0D
#define REG_SYSTEM_0E   0x0E
#define REG_SYSTEM_10   0x10
#define REG_SYSTEM_11   0x11
#define REG_SYSTEM_12   0x12
#define REG_SYSTEM_13   0x13
#define REG_SYSTEM_14   0x14
#define REG_ADC15       0x15
#define REG_ADC16       0x16
#define REG_ADC17       0x17
#define REG_ADC1B       0x1B
#define REG_ADC1C       0x1C
#define REG_DAC_MUTE    0x31
#define REG_DAC_VOLUME  0x32
#define REG_DAC37       0x37
#define REG_GPIO44      0x44
#define REG_GP45        0x45
#define REG_CHIP_ID1    0xFD
#define REG_CHIP_ID2    0xFE

static const char *TAG = "ES8311";

struct es8311 {
    bus_i2c_handle_t i2c;
    gpio_num_t pa_gpio;
    bool pa_active_high;
};

static esp_err_t write_reg(es8311_handle_t codec, uint8_t reg, uint8_t value)
{
    return bus_i2c_write_reg(codec->i2c, reg, value);
}

static esp_err_t read_reg(es8311_handle_t codec, uint8_t reg, uint8_t *value)
{
    return bus_i2c_read_reg(codec->i2c, reg, value);
}

static esp_err_t write_sequence(es8311_handle_t codec,
                                const uint8_t sequence[][2], size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = write_reg(codec, sequence[i][0], sequence[i][1]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t configure_dac(es8311_handle_t codec)
{
    const uint8_t bootstrap[][2] = {
        {REG_GPIO44, 0x08}, {REG_GPIO44, 0x08},
        {REG_CLK01, 0x30}, {REG_CLK02, 0x00}, {REG_CLK03, 0x10},
        {REG_ADC16, 0x24}, {REG_CLK04, 0x10}, {REG_CLK05, 0x00},
        {REG_SYSTEM_0B, 0x00}, {REG_SYSTEM_0C, 0x00},
        {REG_SYSTEM_10, 0x1F}, {REG_SYSTEM_11, 0x7F},
        {REG_RESET, 0x80}, {REG_RESET, 0x80}, {REG_CLK01, 0x3F},
        {REG_SYSTEM_13, 0x10}, {REG_ADC1B, 0x0A},
        {REG_ADC1C, 0x6A}, {REG_GPIO44, 0x58},
        /* 4.096 MHz MCLK / 16 kHz, I2S Philips, 16-bit stereo slots. */
        {REG_CLK02, 0x00}, {REG_CLK05, 0x00}, {REG_CLK03, 0x10},
        {REG_CLK04, 0x20}, {REG_CLK07, 0x00}, {REG_CLK08, 0xFF},
        {REG_CLK06, 0x03}, {REG_SDP_IN, 0x0C}, {REG_SDP_OUT, 0x4C},
        /* Start only the DAC path. */
        {REG_ADC17, 0xBF}, {REG_SYSTEM_0E, 0x02},
        {REG_SYSTEM_12, 0x00}, {REG_SYSTEM_14, 0x1A},
        {REG_SYSTEM_0D, 0x01}, {REG_ADC15, 0x40},
        {REG_DAC37, 0x08}, {REG_GP45, 0x00},
    };
    return write_sequence(codec, bootstrap,
                          sizeof(bootstrap) / sizeof(bootstrap[0]));
}

esp_err_t es8311_init(const es8311_config_t *config, es8311_handle_t *handle)
{
    if (!config || !handle || !config->i2c ||
        config->sample_rate_hz != 16000 || config->mclk_hz != 4096000) {
        return ESP_ERR_INVALID_ARG;
    }
    es8311_handle_t codec = calloc(1, sizeof(*codec));
    if (!codec) return ESP_ERR_NO_MEM;
    codec->i2c = config->i2c;
    codec->pa_gpio = config->pa_gpio;
    codec->pa_active_high = config->pa_active_high;

    const gpio_config_t pa_config = {
        .pin_bit_mask = 1ULL << config->pa_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pa_config);
    if (err == ESP_OK) {
        err = gpio_set_level(config->pa_gpio,
                             config->pa_active_high ? 0 : 1);
    }
    if (err == ESP_OK) err = configure_dac(codec);
    if (err == ESP_OK) err = es8311_set_volume(codec, 65);
    if (err == ESP_OK) err = es8311_set_mute(codec, true);
    if (err == ESP_OK) {
        err = gpio_set_level(config->pa_gpio,
                             config->pa_active_high ? 1 : 0);
    }
    if (err != ESP_OK) {
        gpio_set_level(config->pa_gpio, config->pa_active_high ? 0 : 1);
        free(codec);
        return err;
    }

    uint8_t id1 = 0, id2 = 0;
    if (read_reg(codec, REG_CHIP_ID1, &id1) == ESP_OK &&
        read_reg(codec, REG_CHIP_ID2, &id2) == ESP_OK) {
        ESP_LOGI(TAG, "Codec detectado, ID=%02X:%02X", id1, id2);
    }
    *handle = codec;
    return ESP_OK;
}

esp_err_t es8311_set_volume(es8311_handle_t codec, uint8_t volume)
{
    if (!codec || volume > 100) return ESP_ERR_INVALID_ARG;
    /* Match esp_codec_dev's 0..100 curve and the board's 3.3 V / 5 V gain. */
    float db = volume == 0 ? -96.0f : -50.0f + (float)volume * 0.5f;
    float codec_db = db + 3.609f;
    int reg = (int)((codec_db + 95.5f) * 2.0f);
    if (reg < 0) reg = 0;
    if (reg > 255) reg = 255;
    return write_reg(codec, REG_DAC_VOLUME, (uint8_t)reg);
}

esp_err_t es8311_set_mute(es8311_handle_t codec, bool mute)
{
    if (!codec) return ESP_ERR_INVALID_ARG;
    uint8_t value = 0;
    esp_err_t err = read_reg(codec, REG_DAC_MUTE, &value);
    if (err != ESP_OK) return err;
    value &= 0x9F;
    if (mute) value |= 0x60;
    return write_reg(codec, REG_DAC_MUTE, value);
}

void es8311_deinit(es8311_handle_t codec)
{
    if (!codec) return;
    es8311_set_mute(codec, true);
    gpio_set_level(codec->pa_gpio, codec->pa_active_high ? 0 : 1);
    free(codec);
}
