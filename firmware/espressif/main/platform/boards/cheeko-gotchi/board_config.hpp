// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/spi_common.h"

// Cheeko Gotchi (OSTB_XIAOZHI_V1.2): ESP32-S3R8, 16 MB flash, native USB.
// Pins come from the board schematic and the factory hardware-test firmware,
// which exercises every one of them on this revision.
namespace micropixel::platform::cheeko_gotchi::board {

// The JD9853 panel is 240x296 portrait. MicroPixel presents it as 296x240
// landscape, the orientation the Cheeko firmware uses on the same enclosure.
inline constexpr int32_t kDisplayWidth = 296;
inline constexpr int32_t kDisplayHeight = 240;

// Soft power latch. The PFC161 power controller cuts the rail when this line
// is released high, so it is driven low before anything else on battery.
inline constexpr gpio_num_t kPowerLatch = GPIO_NUM_2;

inline constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
inline constexpr gpio_num_t kI2cScl = GPIO_NUM_11;
inline constexpr gpio_num_t kI2cSda = GPIO_NUM_12;

inline constexpr spi_host_device_t kLcdSpiHost = SPI2_HOST;
inline constexpr gpio_num_t kLcdClock = GPIO_NUM_9;
inline constexpr gpio_num_t kLcdMosi = GPIO_NUM_10;
inline constexpr gpio_num_t kLcdChipSelect = GPIO_NUM_14;
inline constexpr gpio_num_t kLcdDataCommand = GPIO_NUM_8;
// Active low, and also the only reset line of the CST810 touch controller.
inline constexpr gpio_num_t kLcdReset = GPIO_NUM_17;
// Drives the NPN that switches the LED string's low side: active high.
inline constexpr gpio_num_t kBacklight = GPIO_NUM_13;

inline constexpr gpio_num_t kI2sMasterClock = GPIO_NUM_5;
inline constexpr gpio_num_t kI2sBitClock = GPIO_NUM_15;
inline constexpr gpio_num_t kI2sWordSelect = GPIO_NUM_16;
inline constexpr gpio_num_t kI2sDataOut = GPIO_NUM_6;
// NS4150B class-D enable, active high with a pull-down on the board.
inline constexpr gpio_num_t kAmplifierEnable = GPIO_NUM_4;

inline constexpr gpio_num_t kBootButton = GPIO_NUM_0;
inline constexpr gpio_num_t kVolumeDownButton = GPIO_NUM_39;
inline constexpr gpio_num_t kVolumeUpButton = GPIO_NUM_40;

}  // namespace micropixel::platform::cheeko_gotchi::board
