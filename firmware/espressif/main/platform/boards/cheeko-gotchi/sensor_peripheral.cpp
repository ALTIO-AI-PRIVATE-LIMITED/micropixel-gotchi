// SPDX-License-Identifier: Apache-2.0
#include "platform/boards/cheeko-gotchi/sensor_peripheral.hpp"

namespace micropixel::platform::cheeko_gotchi {

esp_err_t SensorPeripheral::Initialize(i2c_master_bus_handle_t bus, buses::I2cExecutor& i2c_executor) {
    bus_ = bus;
    // Touch polling already owns the bus, so the probe runs on the I2C worker.
    const esp_err_t probe_status = i2c_executor.Invoke(
        buses::I2cExecutor::Priority::kHigh,
        [](void* context) {
            auto* sensors = static_cast<SensorPeripheral*>(context);
            return sensors->acceleration_.Initialize(sensors->bus_);
        },
        this);
    if (probe_status != ESP_OK) {
        return probe_status;
    }
    return peripheral_.Initialize(i2c_executor);
}

int32_t SensorPeripheral::GetInfo(device::PeripheralChannelId channel, micropixel_sensor_info_t& info_out) const {
    return peripheral_.GetInfo(channel, info_out);
}

int32_t SensorPeripheral::Start(device::PeripheralChannelId channel, uint32_t interval_us) {
    return peripheral_.Start(channel, interval_us);
}

int32_t SensorPeripheral::Read(device::PeripheralChannelId channel, device::SensorValues& values_out) {
    return peripheral_.Read(channel, values_out);
}

void SensorPeripheral::Stop(device::PeripheralChannelId channel) { peripheral_.Stop(channel); }

}  // namespace micropixel::platform::cheeko_gotchi
