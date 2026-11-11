#pragma once

#include <cstdint>

#include "audio_codec_ctrl_if.h"
#include "driver/i2c_master.h"

namespace micropixel::platform::buses {
class I2cExecutor;
}

namespace micropixel::platform::audio {

// esp_codec_dev control interface that serializes register access through the
// board's shared I2C executor. `interface` must stay the first member: the
// callbacks recover this object from the interface pointer.
struct CodecI2cControl final {
    audio_codec_ctrl_if_t interface{};
    buses::I2cExecutor* executor{};
    i2c_master_dev_handle_t device{};
    uint8_t address{};
    int port{};
    bool open{};

    // Fills `interface` and marks the control open for `device`.
    void Bind(buses::I2cExecutor& bus_executor, i2c_master_dev_handle_t bus_device, uint8_t device_address,
              int i2c_port);
    [[nodiscard]] const audio_codec_ctrl_if_t* Interface() const { return &interface; }

    static CodecI2cControl& From(const audio_codec_ctrl_if_t* raw);
    static int Open(const audio_codec_ctrl_if_t* raw, void*, int);
    static bool IsOpen(const audio_codec_ctrl_if_t* raw);
    static int Read(const audio_codec_ctrl_if_t* raw, int reg, int reg_len, void* data, int data_len);
    static int Write(const audio_codec_ctrl_if_t* raw, int reg, int reg_len, void* data, int data_len);
    static int Close(const audio_codec_ctrl_if_t* raw);
};

// Probes `address` on `bus` through the executor; ESP_OK when it acknowledges.
[[nodiscard]] esp_err_t ProbeCodec(buses::I2cExecutor& executor, i2c_master_bus_handle_t bus, uint8_t address);

}  // namespace micropixel::platform::audio
