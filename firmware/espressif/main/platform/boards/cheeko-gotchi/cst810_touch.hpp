// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

namespace micropixel::platform::cheeko_gotchi {

// Seven-bit application address of the factory-programmed CST810.
inline constexpr uint8_t kCst810Address = 0x15U;

// Builds an esp_lcd_touch handle for the CST810, a single-point Hynitron
// controller with the CST816 report layout. It has no reset line of its own:
// the panel reset pulse also resets it, so the panel must be reset first.
// Coordinates are reported in the landscape frame the board presents.
[[nodiscard]] esp_err_t NewCst810Touch(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t& config,
                                       esp_lcd_touch_handle_t* touch_out);

}  // namespace micropixel::platform::cheeko_gotchi
