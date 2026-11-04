// SPDX-FileCopyrightText: 2026 wamr-host contributors
// SPDX-License-Identifier: Apache-2.0

#include "platform/boards/cheeko-gotchi/battery_peripheral.hpp"

#include <array>

#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_log.h"
#include "platform/boards/cheeko-gotchi/board_config.hpp"

namespace micropixel::platform::cheeko_gotchi {
namespace {

constexpr char kTag[] = "gotchi_battery";
// Half of a 4.2 V cell is 2.1 V, which needs the widest attenuation.
constexpr adc_atten_t kAttenuation = ADC_ATTEN_DB_12;
constexpr uint32_t kDividerRatio = 2U;
constexpr uint32_t kSampleCount = 16U;
constexpr uint64_t kRefreshIntervalUs = 3ULL * 1000ULL * 1000ULL;
// A pack reading outside this window is a floating pin or no cell.
constexpr uint32_t kMinimumPackMillivolts = 3000U;
constexpr uint32_t kMaximumPackMillivolts = 4500U;

struct CurvePoint final {
    uint32_t millivolts;
    uint32_t percent;
};
// Resting discharge curve of a typical single Li-ion/LiPo cell.
constexpr std::array<CurvePoint, 11> kCurve{{{3300, 0},
                                             {3450, 5},
                                             {3550, 10},
                                             {3650, 20},
                                             {3720, 30},
                                             {3770, 40},
                                             {3810, 50},
                                             {3870, 60},
                                             {3950, 70},
                                             {4050, 85},
                                             {4150, 100}}};

[[nodiscard]] uint8_t PercentFromPack(uint32_t millivolts) {
    if (millivolts <= kCurve.front().millivolts) {
        return 0U;
    }
    for (size_t i = 1; i < kCurve.size(); ++i) {
        if (millivolts <= kCurve[i].millivolts) {
            const CurvePoint& lo = kCurve[i - 1];
            const CurvePoint& hi = kCurve[i];
            return static_cast<uint8_t>(lo.percent + (millivolts - lo.millivolts) * (hi.percent - lo.percent) /
                                                         (hi.millivolts - lo.millivolts));
        }
    }
    return 100U;
}

}  // namespace

BatteryPeripheral::~BatteryPeripheral() {
    if (refresh_timer_ != nullptr) {
        (void)esp_timer_stop(refresh_timer_);
        (void)esp_timer_delete(refresh_timer_);
    }
    if (calibration_ != nullptr) {
        (void)adc_cali_delete_scheme_curve_fitting(calibration_);
    }
    if (unit_ != nullptr) {
        (void)adc_oneshot_del_unit(unit_);
    }
}

esp_err_t BatteryPeripheral::Initialize() {
    // The board object may live in PSRAM, where a FreeRTOS spinlock cannot, so
    // the mutex comes from the internal heap.
    lock_ = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(lock_ != nullptr, ESP_ERR_NO_MEM, kTag, "create battery lock failed");
    last_snapshot_.external_power_available = true;


    // The board init task runs with a tight stack, so the ADC probe and every
    // sample run on the esp_timer task instead.
    esp_timer_create_args_t arguments{};
    arguments.callback = RefreshTimer;
    arguments.arg = this;
    arguments.dispatch_method = ESP_TIMER_TASK;
    arguments.name = "gotchi_battery";
    arguments.skip_unhandled_events = true;
    ESP_RETURN_ON_ERROR(esp_timer_create(&arguments, &refresh_timer_), kTag, "create battery timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(refresh_timer_, kRefreshIntervalUs), kTag,
                        "start battery timer failed");
    return ESP_OK;
}

void BatteryPeripheral::Probe() {
    probed_ = true;
    // Try each candidate sense pin; keep the first that reads like half a cell.
    for (const gpio_num_t pin : board::kBatterySenseCandidates) {
        adc_unit_t unit_id{};
        adc_channel_t channel{};
        if (adc_oneshot_io_to_channel(pin, &unit_id, &channel) != ESP_OK) {
            continue;
        }
        adc_oneshot_unit_init_cfg_t unit_config{};
        unit_config.unit_id = unit_id;
        if (adc_oneshot_new_unit(&unit_config, &unit_) != ESP_OK) {
            unit_ = nullptr;
            continue;
        }
        adc_oneshot_chan_cfg_t channel_config{};
        channel_config.atten = kAttenuation;
        channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_cali_curve_fitting_config_t calibration_config{};
        calibration_config.unit_id = unit_id;
        calibration_config.chan = channel;
        calibration_config.atten = kAttenuation;
        calibration_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        const bool configured = adc_oneshot_config_channel(unit_, channel, &channel_config) == ESP_OK;
        if (configured &&
            adc_cali_create_scheme_curve_fitting(&calibration_config, &calibration_) == ESP_OK) {
            unit_id_ = unit_id;
            channel_ = channel;
            uint32_t sense = 0U;
            const esp_err_t read = ReadSenseMillivolts(sense);
            const uint32_t pack = sense * kDividerRatio;
            ESP_LOGI(kTag, "probe GPIO%d: raw=%d sense=%umV pack=%umV (%s)", static_cast<int>(pin), last_raw_, sense,
                     pack, esp_err_to_name(read));
            if (read == ESP_OK && pack >= kMinimumPackMillivolts && pack <= kMaximumPackMillivolts) {
                sense_pin_ = pin;
                filtered_millivolts_ = pack;
                break;
            }
        }
        if (calibration_ != nullptr) {
            (void)adc_cali_delete_scheme_curve_fitting(calibration_);
            calibration_ = nullptr;
        }
        (void)adc_oneshot_del_unit(unit_);
        unit_ = nullptr;
    }
    if (sense_pin_ == GPIO_NUM_NC) {
        ESP_LOGW(kTag, "no battery sense pin found; battery level unavailable");
    } else {
        ESP_LOGI(kTag, "battery sense on GPIO%d", static_cast<int>(sense_pin_));
    }

}

esp_err_t BatteryPeripheral::ReadSenseMillivolts(uint32_t& millivolts) {
    if (unit_ == nullptr || calibration_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t total = 0U;
    for (uint32_t i = 0; i < kSampleCount; ++i) {
        int raw = 0;
        int value = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(unit_, channel_, &raw), kTag, "read battery sense failed");
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(calibration_, raw, &value), kTag, "convert battery sense failed");
        total += static_cast<uint32_t>(value < 0 ? 0 : value);
        last_raw_ = raw;
    }
    millivolts = total / kSampleCount;
    return ESP_OK;
}

device::BatterySnapshot BatteryPeripheral::Snapshot() {
    // Host polls may come from any task, so they read the cached snapshot the
    // timer task keeps fresh.
    if (lock_ == nullptr || xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return {};
    }
    const device::BatterySnapshot snapshot = last_snapshot_;
    xSemaphoreGive(lock_);
    return snapshot;
}

device::BatterySnapshot BatteryPeripheral::Sample() {
    if (lock_ == nullptr || xSemaphoreTake(lock_, pdMS_TO_TICKS(200)) != pdTRUE) {
        return last_snapshot_;
    }
    const device::BatterySnapshot previous = last_snapshot_;
    // The charger's CHRG line lands on GPIO37, which the octal PSRAM of the
    // ESP32-S3R8 owns, so the charge state cannot be read on this board.
    // Reported as "on battery" so the Host's 5-minute idle sleep runs; this
    // board cannot tell USB power apart.
    last_snapshot_.charging_available = false;
    last_snapshot_.external_power_available = true;
    last_snapshot_.external_power_connected = false;

    uint32_t sense = 0U;
    if (sense_pin_ != GPIO_NUM_NC && ReadSenseMillivolts(sense) == ESP_OK) {
        const uint32_t pack = sense * kDividerRatio;
        if (pack >= kMinimumPackMillivolts && pack <= kMaximumPackMillivolts) {
            // A slow average keeps the level from jumping with speaker load.
            filtered_millivolts_ = (filtered_millivolts_ * 3U + pack) / 4U;
            last_snapshot_.percent = PercentFromPack(filtered_millivolts_);
            last_snapshot_.available = true;
        }
    }
    const device::BatterySnapshot current = last_snapshot_;
    xSemaphoreGive(lock_);

    if (previous.percent != current.percent || previous.available != current.available) {
        ESP_LOGI(kTag, "pack=%umV level=%u%%", filtered_millivolts_, static_cast<unsigned>(current.percent));
        device::BatteryStateChangeSink sink = state_change_sink_.load(std::memory_order_acquire);
        if (sink != nullptr) {
            sink(state_change_context_.load(std::memory_order_acquire));
        }
    }
    return current;
}

void BatteryPeripheral::RefreshTimer(void* context) {
    auto* battery = static_cast<BatteryPeripheral*>(context);
    if (battery == nullptr) {
        return;
    }
    if (!battery->probed_) {
        battery->Probe();
    }
    (void)battery->Sample();
}

void BatteryPeripheral::SetStateChangeSink(device::BatteryStateChangeSink sink, void* context) {
    if (sink == nullptr) {
        state_change_sink_.store(nullptr, std::memory_order_release);
        state_change_context_.store(nullptr, std::memory_order_release);
        return;
    }
    state_change_context_.store(context, std::memory_order_release);
    state_change_sink_.store(sink, std::memory_order_release);
}

}  // namespace micropixel::platform::cheeko_gotchi
