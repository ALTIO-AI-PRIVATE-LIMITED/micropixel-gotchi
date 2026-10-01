// SPDX-License-Identifier: Apache-2.0
#include "platform/boards/cheeko-gotchi/cst810_touch.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include "esp_check.h"
#include "esp_lcd_panel_io.h"

namespace micropixel::platform::cheeko_gotchi {
namespace {

constexpr char kTag[] = "cst810";
// Finger count, then X and Y as 12-bit big-endian values.
constexpr uint8_t kFingerCountRegister = 0x02U;
constexpr uint8_t kSingleTrack = 0U;

esp_err_t ReadReport(esp_lcd_touch_handle_t touch) {
    if (touch == nullptr || touch->io == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t report[5]{};
    if (esp_lcd_panel_io_rx_param(touch->io, kFingerCountRegister, report, sizeof(report)) != ESP_OK) {
        // The CST810 NAKs while it is between scans or dozing. Report that as
        // an invalid frame so the input adapter keeps the last state and
        // releases a stale press itself, instead of treating it as a fault.
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint8_t fingers = report[0] & 0x0fU;
    if (fingers > 1U) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const uint16_t x = static_cast<uint16_t>(((report[1] & 0x0fU) << 8U) | report[2]);
    const uint16_t y = static_cast<uint16_t>(((report[3] & 0x0fU) << 8U) | report[4]);
    portENTER_CRITICAL(&touch->data.lock);
    touch->data.points = fingers;
    if (fingers != 0U) {
        touch->data.coords[0] = {
            .track_id = kSingleTrack,
            .x = std::min<uint16_t>(x, static_cast<uint16_t>(touch->config.x_max - 1U)),
            .y = std::min<uint16_t>(y, static_cast<uint16_t>(touch->config.y_max - 1U)),
            .strength = 0U,
        };
    }
    portEXIT_CRITICAL(&touch->data.lock);
    return ESP_OK;
}

bool GetXy(esp_lcd_touch_handle_t touch, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count,
           uint8_t max_points) {
    portENTER_CRITICAL(&touch->data.lock);
    const uint8_t count = std::min<uint8_t>(touch->data.points, max_points);
    for (uint8_t index = 0U; index < count; ++index) {
        x[index] = touch->data.coords[index].x;
        y[index] = touch->data.coords[index].y;
        if (strength != nullptr) {
            strength[index] = touch->data.coords[index].strength;
        }
    }
    *point_count = count;
    portEXIT_CRITICAL(&touch->data.lock);
    return count != 0U;
}

esp_err_t GetTrackId(esp_lcd_touch_handle_t touch, uint8_t* track_id, uint8_t point_count) {
    portENTER_CRITICAL(&touch->data.lock);
    for (uint8_t index = 0U; index < point_count; ++index) {
        track_id[index] = touch->data.coords[index].track_id;
    }
    portEXIT_CRITICAL(&touch->data.lock);
    return ESP_OK;
}

esp_err_t Delete(esp_lcd_touch_handle_t touch) {
    std::free(touch);
    return ESP_OK;
}

}  // namespace

esp_err_t NewCst810Touch(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t& config,
                         esp_lcd_touch_handle_t* touch_out) {
    ESP_RETURN_ON_FALSE(io != nullptr && touch_out != nullptr && config.x_max != 0U && config.y_max != 0U,
                        ESP_ERR_INVALID_ARG, kTag, "invalid argument");
    auto* touch = static_cast<esp_lcd_touch_handle_t>(std::calloc(1U, sizeof(esp_lcd_touch_t)));
    ESP_RETURN_ON_FALSE(touch != nullptr, ESP_ERR_NO_MEM, kTag, "no memory for touch handle");
    touch->io = io;
    touch->config = config;
    portMUX_INITIALIZE(&touch->data.lock);
    touch->read_data = ReadReport;
    touch->get_xy = GetXy;
    touch->get_track_id = GetTrackId;
    touch->del = Delete;
    *touch_out = touch;
    return ESP_OK;
}

}  // namespace micropixel::platform::cheeko_gotchi
