#include "platform/audio/codec_i2c_control.hpp"

#include <cstring>

#include "esp_codec_dev.h"
#include "platform/buses/i2c_executor.hpp"

namespace micropixel::platform::audio {
namespace {

constexpr int kI2cTimeoutMs = 100;

}  // namespace

void CodecI2cControl::Bind(buses::I2cExecutor& bus_executor, i2c_master_dev_handle_t bus_device,
                           uint8_t device_address, int i2c_port) {
    interface = {};
    interface.open = Open;
    interface.is_open = IsOpen;
    interface.read_reg = Read;
    interface.write_reg = Write;
    auto configure_optional_info = []<typename Interface>(Interface& raw_interface) {
        if constexpr (requires { raw_interface.get_info; }) {
            raw_interface.get_info = [](const audio_codec_ctrl_if_t* raw, auto* info) -> int {
                if (raw == nullptr || info == nullptr) {
                    return ESP_CODEC_DEV_INVALID_ARG;
                }
                const CodecI2cControl& control = From(raw);
                *info = {};
                info->type = static_cast<decltype(info->type)>(1);
                info->i2c.addr = static_cast<uint16_t>(control.address << 1U);
                info->i2c.port = static_cast<uint8_t>(control.port);
                return ESP_CODEC_DEV_OK;
            };
        }
    };
    configure_optional_info(interface);
    interface.close = Close;
    executor = &bus_executor;
    device = bus_device;
    address = device_address;
    port = i2c_port;
    open = true;
}

CodecI2cControl& CodecI2cControl::From(const audio_codec_ctrl_if_t* raw) {
    return *reinterpret_cast<CodecI2cControl*>(const_cast<audio_codec_ctrl_if_t*>(raw));
}

int CodecI2cControl::Open(const audio_codec_ctrl_if_t* interface, void*, int) {
    CodecI2cControl& control = From(interface);
    control.open = control.executor != nullptr && control.device != nullptr;
    return control.open ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRONG_STATE;
}

bool CodecI2cControl::IsOpen(const audio_codec_ctrl_if_t* interface) {
    return interface != nullptr && From(interface).open;
}

int CodecI2cControl::Read(const audio_codec_ctrl_if_t* interface, int reg, int reg_len, void* data,
                          int data_len) {
    if (interface == nullptr || data == nullptr || reg_len < 1 || reg_len > 2 || data_len < 1) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    CodecI2cControl& control = From(interface);
    if (!control.open || control.executor == nullptr || control.device == nullptr) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    struct Request final {
        i2c_master_dev_handle_t device;
        uint8_t address[2];
        int address_length;
        void* data;
        int data_length;
    } request{
        .device = control.device,
        .address = {static_cast<uint8_t>(reg_len > 1 ? reg >> 8 : reg), static_cast<uint8_t>(reg)},
        .address_length = reg_len,
        .data = data,
        .data_length = data_len,
    };
    if (reg_len == 1) {
        request.address[0] = static_cast<uint8_t>(reg);
    }
    const esp_err_t status = control.executor->Invoke(
        buses::I2cExecutor::Priority::kHigh,
        [](void* context) {
            auto& requested = *static_cast<Request*>(context);
            return i2c_master_transmit_receive(requested.device, requested.address, requested.address_length,
                                               static_cast<uint8_t*>(requested.data), requested.data_length,
                                               kI2cTimeoutMs);
        },
        &request);
    return status == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

int CodecI2cControl::Write(const audio_codec_ctrl_if_t* interface, int reg, int reg_len, void* data,
                           int data_len) {
    if (interface == nullptr || data == nullptr || reg_len < 1 || reg_len > 2 || data_len < 0 ||
        reg_len + data_len > 8) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    CodecI2cControl& control = From(interface);
    if (!control.open || control.executor == nullptr || control.device == nullptr) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    struct Request final {
        i2c_master_dev_handle_t device;
        uint8_t bytes[8];
        int length;
    } request{.device = control.device, .bytes = {}, .length = reg_len + data_len};
    if (reg_len > 1) {
        request.bytes[0] = static_cast<uint8_t>(reg >> 8);
        request.bytes[1] = static_cast<uint8_t>(reg);
    } else {
        request.bytes[0] = static_cast<uint8_t>(reg);
    }
    std::memcpy(request.bytes + reg_len, data, static_cast<size_t>(data_len));
    const esp_err_t status = control.executor->Invoke(
        buses::I2cExecutor::Priority::kHigh,
        [](void* context) {
            auto& requested = *static_cast<Request*>(context);
            return i2c_master_transmit(requested.device, requested.bytes, requested.length, kI2cTimeoutMs);
        },
        &request);
    return status == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

int CodecI2cControl::Close(const audio_codec_ctrl_if_t* interface) {
    if (interface == nullptr) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    From(interface).open = false;
    return ESP_CODEC_DEV_OK;
}

esp_err_t ProbeCodec(buses::I2cExecutor& executor, i2c_master_bus_handle_t bus, uint8_t address) {
    struct ProbeRequest final {
        i2c_master_bus_handle_t bus;
        uint16_t address;
    } request{.bus = bus, .address = address};
    return executor.Invoke(
        buses::I2cExecutor::Priority::kHigh,
        [](void* context) {
            const auto& requested = *static_cast<ProbeRequest*>(context);
            return i2c_master_probe(requested.bus, requested.address, kI2cTimeoutMs);
        },
        &request);
}

}  // namespace micropixel::platform::audio
