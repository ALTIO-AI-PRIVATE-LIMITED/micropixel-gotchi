#pragma once

#include "driver/gpio.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

// Middle key (also the power key): active high. Vol-/Vol+: active low.
#define BOARD_BUTTON_MIDDLE   GPIO_NUM_3
#define BOARD_BUTTON_VOL_DOWN GPIO_NUM_39
#define BOARD_BUTTON_VOL_UP   GPIO_NUM_40

/// Power latch, buttons, I2C, I2S, ES8311 (speaker) and ES7210 (mics).
void board_init(void);

/// Release the soft power latch (turns the Gotchi off on battery).
void board_power_off(void);

esp_codec_dev_handle_t board_get_playback_handle(void);
esp_codec_dev_handle_t board_get_record_handle(void);

#ifdef __cplusplus
}
#endif
