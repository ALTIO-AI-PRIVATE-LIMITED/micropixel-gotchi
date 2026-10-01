// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace micropixel::platform::cheeko_gotchi {

// Board-wide resources that several peripherals share: the power latch, the
// single I2C bus (touch, codecs, accelerometer) and the backlight PWM.
class BoardHardware final {
   public:
    // Holds the power latch first so a battery-powered unit stays on.
    [[nodiscard]] esp_err_t Initialize();
    [[nodiscard]] esp_err_t SetBrightness(int percent);
    [[nodiscard]] i2c_master_bus_handle_t I2cBus() const { return i2c_bus_; }

   private:
    i2c_master_bus_handle_t i2c_bus_{};
    bool brightness_initialized_{};
};

}  // namespace micropixel::platform::cheeko_gotchi
