// SPDX-License-Identifier: Apache-2.0
#include "platform/boards/cheeko-gotchi/jd9853_panel.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace micropixel::platform::cheeko_gotchi {
namespace {

constexpr char kTag[] = "jd9853";

constexpr uint8_t kMadctlRowOrder = 0x80U;     // MY
constexpr uint8_t kMadctlColumnOrder = 0x40U;  // MX
constexpr uint8_t kMadctlRowColumn = 0x20U;    // MV
constexpr uint8_t kMadctlBgr = 0x08U;
constexpr uint8_t kColmodRgb565 = 0x05U;
constexpr uint32_t kResetPulseMs = 10U;
// Both the panel and the CST810 sharing its reset line need this long before
// they accept commands.
constexpr uint32_t kResetSettleMs = 120U;
constexpr uint32_t kSleepOutSettleMs = 120U;

struct InitCommand final {
    uint8_t command;
    uint8_t data[32];
    uint8_t size;
};

// The panel maker's sequence for the BOE WV020JAU-N80, as run by the factory
// hardware test and the Cheeko firmware: password unlock, page-1 power and
// gamma, back to page 0, TE on and RGB565. MADCTL and SLPOUT follow it.
constexpr InitCommand kVendorInit[] = {
    {0xDFU, {0x98U, 0x53U}, 2U},
    {0xDEU, {0x00U}, 1U},
    {0xB2U, {0x25U}, 1U},
    {0xB7U, {0x00U, 0x29U, 0x00U, 0x51U}, 4U},
    {0xBBU, {0x4FU, 0x1AU, 0x55U, 0x73U, 0x63U, 0xF0U}, 6U},
    {0xC0U, {0x44U, 0xA4U}, 2U},
    {0xC1U, {0x12U}, 1U},
    {0xC3U, {0x7DU, 0x07U, 0x14U, 0x06U, 0xC8U, 0x71U, 0x6CU, 0x77U}, 8U},
    {0xC4U, {0x00U, 0x00U, 0x94U, 0x79U, 0x25U, 0x0AU, 0x16U, 0x79U, 0x25U, 0x0AU, 0x16U, 0x82U}, 12U},
    {0xC8U,
     {0x3FU, 0x34U, 0x2DU, 0x26U, 0x2BU, 0x2BU, 0x25U, 0x24U, 0x23U, 0x22U, 0x20U, 0x17U, 0x14U, 0x0EU, 0x06U, 0x00U,
      0x3FU, 0x34U, 0x2DU, 0x26U, 0x2BU, 0x2BU, 0x25U, 0x24U, 0x23U, 0x22U, 0x20U, 0x17U, 0x14U, 0x0EU, 0x06U, 0x00U},
     32U},
    {0xD0U, {0x04U, 0x06U, 0x6BU, 0x0FU, 0x00U}, 5U},
    {0xD7U, {0x00U, 0x30U}, 2U},
    {0xE6U, {0x10U}, 1U},
    {0xDEU, {0x01U}, 1U},
    {0xB7U, {0x03U, 0x13U, 0xEFU, 0x35U, 0x35U}, 5U},
    {0xC1U, {0x14U, 0x15U, 0xC0U}, 3U},
    {0xC2U, {0x06U, 0x3AU}, 2U},
    {0xC4U, {0x72U, 0x12U}, 2U},
    {0xBEU, {0x00U}, 1U},
    {0xDEU, {0x00U}, 1U},
    {LCD_CMD_TEON, {0x00U}, 1U},
    {LCD_CMD_COLMOD, {kColmodRgb565}, 1U},
};

struct Jd9853Panel final {
    esp_lcd_panel_t base{};  // Must stay first: esp_lcd hands back &base.
    esp_lcd_panel_io_handle_t io{};
    gpio_num_t reset_gpio{GPIO_NUM_NC};
    bool reset_active_high{};
    int x_gap{};
    int y_gap{};
    uint8_t madctl{};
};

Jd9853Panel& From(esp_lcd_panel_t* panel) { return *reinterpret_cast<Jd9853Panel*>(panel); }

esp_err_t WriteMadctl(Jd9853Panel& panel) {
    return esp_lcd_panel_io_tx_param(panel.io, LCD_CMD_MADCTL, &panel.madctl, 1U);
}

void SetMadctlBit(Jd9853Panel& panel, uint8_t bit, bool enabled) {
    panel.madctl = enabled ? static_cast<uint8_t>(panel.madctl | bit) : static_cast<uint8_t>(panel.madctl & ~bit);
}

esp_err_t Delete(esp_lcd_panel_t* panel) {
    Jd9853Panel* jd = &From(panel);
    if (jd->reset_gpio != GPIO_NUM_NC) {
        (void)gpio_reset_pin(jd->reset_gpio);
    }
    jd->~Jd9853Panel();
    std::free(jd);
    return ESP_OK;
}

esp_err_t Reset(esp_lcd_panel_t* panel) {
    Jd9853Panel& jd = From(panel);
    if (jd.reset_gpio != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(jd.reset_gpio, jd.reset_active_high ? 1U : 0U), kTag, "assert reset");
        vTaskDelay(pdMS_TO_TICKS(kResetPulseMs));
        ESP_RETURN_ON_ERROR(gpio_set_level(jd.reset_gpio, jd.reset_active_high ? 0U : 1U), kTag, "release reset");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, LCD_CMD_SWRESET, nullptr, 0U), kTag, "software reset");
    }
    vTaskDelay(pdMS_TO_TICKS(kResetSettleMs));
    return ESP_OK;
}

esp_err_t Init(esp_lcd_panel_t* panel) {
    Jd9853Panel& jd = From(panel);
    for (const auto& command : kVendorInit) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, command.command, command.data, command.size), kTag,
                            "init command 0x%02x failed", command.command);
    }
    ESP_RETURN_ON_ERROR(WriteMadctl(jd), kTag, "MADCTL failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, LCD_CMD_SLPOUT, nullptr, 0U), kTag, "sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(kSleepOutSettleMs));
    return ESP_OK;
}

esp_err_t DrawBitmap(esp_lcd_panel_t* panel, int x_start, int y_start, int x_end, int y_end, const void* color_data) {
    Jd9853Panel& jd = From(panel);
    ESP_RETURN_ON_FALSE(x_start < x_end && y_start < y_end, ESP_ERR_INVALID_ARG, kTag, "empty draw area");
    x_start += jd.x_gap;
    x_end += jd.x_gap;
    y_start += jd.y_gap;
    y_end += jd.y_gap;
    const uint8_t columns[] = {
        static_cast<uint8_t>(x_start >> 8),
        static_cast<uint8_t>(x_start),
        static_cast<uint8_t>((x_end - 1) >> 8),
        static_cast<uint8_t>(x_end - 1),
    };
    const uint8_t rows[] = {
        static_cast<uint8_t>(y_start >> 8),
        static_cast<uint8_t>(y_start),
        static_cast<uint8_t>((y_end - 1) >> 8),
        static_cast<uint8_t>(y_end - 1),
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, LCD_CMD_CASET, columns, sizeof(columns)), kTag, "CASET");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, LCD_CMD_RASET, rows, sizeof(rows)), kTag, "RASET");
    const size_t bytes = static_cast<size_t>(x_end - x_start) * static_cast<size_t>(y_end - y_start) * 2U;
    return esp_lcd_panel_io_tx_color(jd.io, LCD_CMD_RAMWR, color_data, bytes);
}

esp_err_t InvertColor(esp_lcd_panel_t* panel, bool invert) {
    return esp_lcd_panel_io_tx_param(From(panel).io, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, nullptr, 0U);
}

esp_err_t Mirror(esp_lcd_panel_t* panel, bool mirror_x, bool mirror_y) {
    Jd9853Panel& jd = From(panel);
    SetMadctlBit(jd, kMadctlColumnOrder, mirror_x);
    SetMadctlBit(jd, kMadctlRowOrder, mirror_y);
    return WriteMadctl(jd);
}

esp_err_t SwapXy(esp_lcd_panel_t* panel, bool swap_axes) {
    Jd9853Panel& jd = From(panel);
    SetMadctlBit(jd, kMadctlRowColumn, swap_axes);
    return WriteMadctl(jd);
}

esp_err_t SetGap(esp_lcd_panel_t* panel, int x_gap, int y_gap) {
    Jd9853Panel& jd = From(panel);
    jd.x_gap = x_gap;
    jd.y_gap = y_gap;
    return ESP_OK;
}

esp_err_t DisplayOnOff(esp_lcd_panel_t* panel, bool on) {
    return esp_lcd_panel_io_tx_param(From(panel).io, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, nullptr, 0U);
}

esp_err_t DisplaySleep(esp_lcd_panel_t* panel, bool sleep) {
    Jd9853Panel& jd = From(panel);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(jd.io, sleep ? LCD_CMD_SLPIN : LCD_CMD_SLPOUT, nullptr, 0U), kTag,
                        "sleep command failed");
    vTaskDelay(pdMS_TO_TICKS(kSleepOutSettleMs));
    return ESP_OK;
}

}  // namespace

esp_err_t NewJd9853Panel(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t& config,
                         esp_lcd_panel_handle_t* panel_out) {
    ESP_RETURN_ON_FALSE(io != nullptr && panel_out != nullptr, ESP_ERR_INVALID_ARG, kTag, "invalid argument");
    ESP_RETURN_ON_FALSE(config.bits_per_pixel == 16U, ESP_ERR_NOT_SUPPORTED, kTag, "only RGB565 is supported");
    void* storage = std::calloc(1U, sizeof(Jd9853Panel));
    ESP_RETURN_ON_FALSE(storage != nullptr, ESP_ERR_NO_MEM, kTag, "no memory for panel");
    auto* jd = new (storage) Jd9853Panel{};
    jd->io = io;
    jd->reset_gpio = static_cast<gpio_num_t>(config.reset_gpio_num);
    jd->reset_active_high = config.flags.reset_active_high != 0U;
    jd->madctl = config.rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR ? kMadctlBgr : 0U;
    if (jd->reset_gpio != GPIO_NUM_NC) {
        gpio_config_t reset_config{};
        reset_config.pin_bit_mask = 1ULL << jd->reset_gpio;
        reset_config.mode = GPIO_MODE_OUTPUT;
        const esp_err_t status = gpio_config(&reset_config);
        if (status != ESP_OK) {
            jd->~Jd9853Panel();
            std::free(jd);
            return status;
        }
    }
    jd->base.del = Delete;
    jd->base.reset = Reset;
    jd->base.init = Init;
    jd->base.draw_bitmap = DrawBitmap;
    jd->base.invert_color = InvertColor;
    jd->base.mirror = Mirror;
    jd->base.swap_xy = SwapXy;
    jd->base.set_gap = SetGap;
    jd->base.disp_on_off = DisplayOnOff;
    jd->base.disp_sleep = DisplaySleep;
    *panel_out = &jd->base;
    return ESP_OK;
}

}  // namespace micropixel::platform::cheeko_gotchi
