// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>

#include "device/contracts/sensors.hpp"
#include "driver/i2c_master.h"
#include "platform/buses/i2c_executor.hpp"
#include "platform/drivers/sensors/sc7a20htr.hpp"
#include "platform/sensors/polled_vector_sensor_peripheral.hpp"

namespace micropixel::platform::cheeko_gotchi {

// The 3-axis accelerometer at 0x19 (SC7A20 or the register-compatible
// LIS2DH12, depending on the build lot).
class SensorPeripheral final : public device::SensorPeripheral {
   public:
    static constexpr device::PeripheralChannelId kAcceleration = 1U;

    // Probes the sensor on the I2C worker, then prepares its polling timer.
    [[nodiscard]] esp_err_t Initialize(i2c_master_bus_handle_t bus, buses::I2cExecutor& i2c_executor);

    [[nodiscard]] bool acceleration_available() const { return acceleration_.available(); }
    [[nodiscard]] int32_t GetInfo(device::PeripheralChannelId channel,
                                  micropixel_sensor_info_t& info_out) const override;
    [[nodiscard]] int32_t Start(device::PeripheralChannelId channel, uint32_t interval_us) override;
    [[nodiscard]] int32_t Read(device::PeripheralChannelId channel, device::SensorValues& values_out) override;
    void Stop(device::PeripheralChannelId channel) override;

   private:
    i2c_master_bus_handle_t bus_{};
    drivers::Sc7a20htr acceleration_{};
    std::array<sensors::PolledVectorSensorPeripheral::Channel, 1> channels_{{
        {kAcceleration, MICROPIXEL_SENSOR_ACCELERATION, acceleration_, "gotchi_accel"},
    }};
    sensors::PolledVectorSensorPeripheral peripheral_{channels_, "gotchi_sensors"};
};

}  // namespace micropixel::platform::cheeko_gotchi
