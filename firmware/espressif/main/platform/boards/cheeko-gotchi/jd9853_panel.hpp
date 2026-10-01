// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_types.h"

namespace micropixel::platform::cheeko_gotchi {

// esp_lcd driver for the JD9853 controller behind the Gotchi's BOE
// WV020JAU-N80 2.01" 240x296 IPS panel. JD9853 is not in ESP-IDF; it accepts
// standard MIPI DCS addressing, so only the vendor power/gamma sequence and
// MADCTL handling are specific. Supports RGB565 only.
[[nodiscard]] esp_err_t NewJd9853Panel(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t& config,
                                       esp_lcd_panel_handle_t* panel_out);

}  // namespace micropixel::platform::cheeko_gotchi
