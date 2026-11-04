// SPDX-FileCopyrightText: 2026 wamr-host contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>

#include "device/contracts/battery.hpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/gpio_types.h"

namespace micropixel::platform::cheeko_gotchi {

// The Cheeko Gotchi has no fuel gauge. The pack reaches the SoC through a
// 470k/470k divider (BAT_DET, so the pin sees half the pack voltage). The
// charger's CHRG line sits on a PSRAM pin, so only the level is reported.
//
// The schematic's BAT_DET net label is ambiguous about which ADC pin it lands
// on, so Initialize() probes the free ADC-capable candidates and keeps the one
// whose reading looks like half a Li-ion cell.
class BatteryPeripheral final : public device::Battery {
   public:
    ~BatteryPeripheral() override;

    [[nodiscard]] esp_err_t Initialize();
    [[nodiscard]] device::BatterySnapshot Snapshot() override;
    void SetStateChangeSink(device::BatteryStateChangeSink sink, void* context) override;

   private:
    void Probe();
    [[nodiscard]] device::BatterySnapshot Sample();
    [[nodiscard]] esp_err_t ReadSenseMillivolts(uint32_t& millivolts);
    static void RefreshTimer(void* context);

    adc_oneshot_unit_handle_t unit_{};
    adc_cali_handle_t calibration_{};
    adc_unit_t unit_id_{};
    adc_channel_t channel_{};
    gpio_num_t sense_pin_{GPIO_NUM_NC};
    SemaphoreHandle_t lock_{};
    device::BatterySnapshot last_snapshot_{};
    uint32_t filtered_millivolts_{};
    esp_timer_handle_t refresh_timer_{};
    bool probed_{};
    int last_raw_{};
    uint32_t debug_samples_{};
    std::atomic<device::BatteryStateChangeSink> state_change_sink_{};
    std::atomic<void*> state_change_context_{};
};

}  // namespace micropixel::platform::cheeko_gotchi
