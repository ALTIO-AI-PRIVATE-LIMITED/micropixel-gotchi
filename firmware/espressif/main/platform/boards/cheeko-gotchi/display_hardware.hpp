// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"

namespace micropixel::platform::cheeko_gotchi {

// The JD9853 panel on SPI2 and the CST810 on the shared I2C bus. The touch
// controller is reset by the panel's reset pulse, so the panel comes first.
class DisplayHardware final {
   public:
    [[nodiscard]] esp_err_t InitializePanel();
    [[nodiscard]] esp_err_t InitializeTouch(i2c_master_bus_handle_t bus);

    [[nodiscard]] esp_lcd_panel_handle_t Panel() const { return panel_; }
    [[nodiscard]] esp_lcd_panel_io_handle_t PanelIo() const { return panel_io_; }
    [[nodiscard]] esp_lcd_touch_handle_t Touch() const { return touch_; }

   private:
    void ReleasePanel(bool bus_initialized);

    esp_lcd_panel_handle_t panel_{};
    esp_lcd_panel_io_handle_t panel_io_{};
    esp_lcd_panel_io_handle_t touch_io_{};
    esp_lcd_touch_handle_t touch_{};
};

}  // namespace micropixel::platform::cheeko_gotchi
