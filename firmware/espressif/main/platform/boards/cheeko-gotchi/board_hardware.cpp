// SPDX-License-Identifier: Apache-2.0
#include "platform/boards/cheeko-gotchi/board_hardware.hpp"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "platform/boards/cheeko-gotchi/board_config.hpp"

namespace micropixel::platform::cheeko_gotchi {
namespace {

constexpr char kTag[] = "gotchi_hw";
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
// Above the audible band: the backlight NPN sits next to the speaker amp.
constexpr uint32_t kBacklightFrequencyHz = 25000U;
constexpr uint32_t kBacklightMaximumDuty = (1U << 10U) - 1U;

}  // namespace

esp_err_t BoardHardware::Initialize() {
    if (i2c_bus_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t latch_config{};
    latch_config.pin_bit_mask = 1ULL << board::kPowerLatch;
    latch_config.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_set_level(board::kPowerLatch, 0), kTag, "preset power latch failed");
    ESP_RETURN_ON_ERROR(gpio_config(&latch_config), kTag, "hold power latch failed");

    // The bus has external 4.7 kOhm pull-ups; the internal ones only help
    // while the codecs are still in reset.
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = board::kI2cPort;
    bus_config.sda_io_num = board::kI2cSda;
    bus_config.scl_io_num = board::kI2cScl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7U;
    bus_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus_), kTag, "create shared I2C bus failed");

    ledc_timer_config_t timer_config{};
    timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    timer_config.timer_num = kBacklightTimer;
    timer_config.freq_hz = kBacklightFrequencyHz;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), kTag, "configure backlight timer failed");
    // Start dark; the board raises brightness after the first frame is drawn.
    ledc_channel_config_t channel_config{};
    channel_config.gpio_num = board::kBacklight;
    channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_config.channel = kBacklightChannel;
    channel_config.timer_sel = kBacklightTimer;
    channel_config.duty = 0U;
    channel_config.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), kTag, "configure backlight channel failed");
    brightness_initialized_ = true;
    return ESP_OK;
}

esp_err_t BoardHardware::SetBrightness(int percent) {
    if (!brightness_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t bounded = static_cast<uint32_t>(std::clamp(percent, 0, 100));
    const uint32_t duty = (kBacklightMaximumDuty * bounded + 50U) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty), kTag, "set backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

}  // namespace micropixel::platform::cheeko_gotchi
