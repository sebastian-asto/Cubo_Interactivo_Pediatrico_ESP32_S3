#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AUDIO_WAV_SAMPLE_RATE 16000U
#define AUDIO_WAV_MAX_SECONDS 15U
#define AUDIO_WAV_MAX_BYTES (44U + AUDIO_WAV_SAMPLE_RATE * AUDIO_WAV_MAX_SECONDS)

/* ES8311 + NS4150B: I2C 42/41, I2S 8/9/10/12 y PA GPIO7. */
esp_err_t audio_player_init(void);
bool audio_player_ready(void);
uint8_t audio_player_get_volume(void);
esp_err_t audio_player_set_volume(uint8_t volume);
esp_err_t audio_player_validate_wav(const uint8_t *wav, size_t size);
esp_err_t audio_player_play_wav(const uint8_t *wav, size_t size);
