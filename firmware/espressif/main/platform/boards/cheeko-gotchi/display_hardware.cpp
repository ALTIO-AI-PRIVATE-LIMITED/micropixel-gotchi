// SPDX-License-Identifier: Apache-2.0
#include "platform/boards/cheeko-gotchi/display_hardware.hpp"

#include <cstddef>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "platform/boards/cheeko-gotchi/board_config.hpp"
#include "platform/boards/cheeko-gotchi/cst810_touch.hpp"
#include "platform/boards/cheeko-gotchi/jd9853_panel.hpp"

namespace micropixel::platform::cheeko_gotchi {
namespace {

constexpr char kTag[] = "gotchi_display";
// The factory firmwares run this panel at 40 MHz, SPI mode 0.
constexpr uint32_t kLcdPixelClockHz = 40U * 1000U * 1000U;
constexpr size_t kPanelIoQueueDepth = 10U;
// Landscape is the portrait panel with rows and columns exchanged and the row
// order reversed. The GRAM is 320 lines long and only 296 reach the glass, so
// the visible window starts 24 lines in once the order is reversed.
constexpr bool kSwapXy = true;
constexpr bool kMirrorX = false;
constexpr bool kMirrorY = true;
constexpr int kColumnGap = 24;
// The hardware test's colour bars render correctly in BGR on this panel batch.
// If red and blue appear exchanged on another batch, this is the one switch.
constexpr lcd_rgb_element_order_t kColorOrder = LCD_RGB_ELEMENT_ORDER_BGR;
// The CST810 intermittently NAKs at 400 kHz while a finger is held.
constexpr uint32_t kTouchI2cClockHz = 100000U;

}  // namespace

void DisplayHardware::ReleasePanel(bool bus_initialized) {
    if (panel_ != nullptr) {
        (void)esp_lcd_panel_del(panel_);
        panel_ = nullptr;
    }
    if (panel_io_ != nullptr) {
        (void)esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    if (bus_initialized) {
        (void)spi_bus_free(board::kLcdSpiHost);
    }
}

esp_err_t DisplayHardware::InitializePanel() {
    if (panel_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    const int transfer_bytes = board::kDisplayWidth * CONFIG_MICROPIXEL_LVGL_PARTIAL_BUFFER_HEIGHT * 2;
    spi_bus_config_t bus_config{};
    bus_config.mosi_io_num = board::kLcdMosi;
    bus_config.miso_io_num = GPIO_NUM_NC;
    bus_config.sclk_io_num = board::kLcdClock;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.data4_io_num = GPIO_NUM_NC;
    bus_config.data5_io_num = GPIO_NUM_NC;
    bus_config.data6_io_num = GPIO_NUM_NC;
    bus_config.data7_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz = transfer_bytes;
    ESP_RETURN_ON_ERROR(spi_bus_initialize(board::kLcdSpiHost, &bus_config, SPI_DMA_CH_AUTO), kTag,
                        "initialize LCD SPI bus failed");

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = board::kLcdChipSelect;
    io_config.dc_gpio_num = board::kLcdDataCommand;
    io_config.spi_mode = 0;
    io_config.pclk_hz = kLcdPixelClockHz;
    io_config.trans_queue_depth = kPanelIoQueueDepth;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.psram_dma_direct = 0;
    esp_err_t status =
        esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(board::kLcdSpiHost), &io_config, &panel_io_);
    if (status != ESP_OK) {
        ReleasePanel(true);
        return status;
    }

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.rgb_ele_order = kColorOrder;
    panel_config.bits_per_pixel = 16;
    panel_config.reset_gpio_num = board::kLcdReset;
    status = NewJd9853Panel(panel_io_, panel_config, &panel_);
    if (status == ESP_OK) {
        status = esp_lcd_panel_reset(panel_);
    }
    if (status == ESP_OK) {
        status = esp_lcd_panel_init(panel_);
    }
    if (status == ESP_OK) {
        status = esp_lcd_panel_swap_xy(panel_, kSwapXy);
    }
    if (status == ESP_OK) {
        status = esp_lcd_panel_mirror(panel_, kMirrorX, kMirrorY);
    }
    if (status == ESP_OK) {
        status = esp_lcd_panel_set_gap(panel_, kColumnGap, 0);
    }
    if (status != ESP_OK) {
        ReleasePanel(true);
        return status;
    }
    ESP_LOGI(kTag, "JD9853 %dx%d panel uses internal SRAM SPI DMA: block=%d bytes clock=%lu MHz",
             static_cast<int>(board::kDisplayWidth), static_cast<int>(board::kDisplayHeight), transfer_bytes,
             static_cast<unsigned long>(kLcdPixelClockHz / 1000000U));
    return ESP_OK;
}

esp_err_t DisplayHardware::InitializeTouch(i2c_master_bus_handle_t bus) {
    if (bus == nullptr || panel_ == nullptr || touch_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, kCst810Address, 50), kTag, "CST810 not found at 0x%02x", kCst810Address);
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = kCst810Address;
    io_config.scl_speed_hz = kTouchI2cClockHz;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = 1;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_config, &touch_io_), kTag, "create CST810 panel IO failed");

    // The controller already reports in the landscape frame: X along the long
    // edge, Y along the short one, in the same direction as the panel.
    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = static_cast<uint16_t>(board::kDisplayWidth);
    touch_config.y_max = static_cast<uint16_t>(board::kDisplayHeight);
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_NC;
    const esp_err_t status = NewCst810Touch(touch_io_, touch_config, &touch_);
    if (status != ESP_OK) {
        (void)esp_lcd_panel_io_del(touch_io_);
        touch_io_ = nullptr;
    }
    return status;
}

}  // namespace micropixel::platform::cheeko_gotchi
