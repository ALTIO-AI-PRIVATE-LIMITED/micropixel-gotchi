#include "platform/audio/es7210_microphone.hpp"

#include <cstdio>
#include <cstdlib>

#include <algorithm>

#include "es7210_adc.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#ifdef CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#include "freertos/idf_additions.h"
#endif
#include "work/task_policy.hpp"

namespace micropixel::platform::audio {
namespace {

constexpr uint32_t kTaskStackSize = 3072U;
constexpr uint32_t kReadTimeoutMs = 100U;
constexpr uint32_t kStopTimeoutMs = 500U;
constexpr uint8_t kKeepDefaultGain = 0xFFU;
// Peak levels are logged for this many seconds after each Start().
constexpr uint32_t kLevelLogSeconds = 3U;

}  // namespace

esp_err_t Es7210Microphone::Configure(i2c_master_bus_handle_t bus, buses::I2cExecutor& executor) {
    if (bus == nullptr || device_ != nullptr || config_.i2c_address == 0U || config_.sample_rate == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t probe_status = ProbeCodec(executor, bus, config_.i2c_address);
    if (probe_status != ESP_OK) {
        ESP_LOGW(config_.log_tag, "%s did not acknowledge at I2C 0x%02x: %s", config_.name, config_.i2c_address,
                 esp_err_to_name(probe_status));
        return probe_status;
    }
    i2c_device_config_t device_config{};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = config_.i2c_address;
    device_config.scl_speed_hz = config_.i2c_clock_hz;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &device_), config_.log_tag,
                        "attach ADC control bus failed");
    control_.Bind(executor, device_, config_.i2c_address, config_.i2c_port);
    idle_ = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(idle_ != nullptr, ESP_ERR_NO_MEM, config_.log_tag, "create idle semaphore failed");
    TaskHandle_t task = nullptr;
#ifdef CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(TaskEntry, "micropixel_mic", kTaskStackSize, this,
                                                               task_policy::kAudioPriority, &task,
                                                               task_policy::kSystemCore,
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    const BaseType_t created = xTaskCreatePinnedToCore(TaskEntry, "micropixel_mic", kTaskStackSize, this,
                                                       task_policy::kAudioPriority, &task, task_policy::kSystemCore);
#endif
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, config_.log_tag, "create capture task failed");
    task_ = task;
    ESP_LOGI(config_.log_tag, "%s found at I2C 0x%02x", config_.name, config_.i2c_address);
    return ESP_OK;
}

esp_err_t Es7210Microphone::ConfigureCodec() {
    if (codec_if_ != nullptr) {
        return ESP_OK;
    }
    // As the Cheeko Gotchi factory firmware: all four inputs selected, which
    // puts the ES7210 in TDM. Slot 0 is MIC1, slot 1 MIC2 (a second mic),
    // slot 2 the speaker loopback.
    es7210_codec_cfg_t codec_config{};
    codec_config.ctrl_if = control_.Interface();
    codec_config.master_mode = false;
    codec_config.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    codec_config.mclk_div = 256U;
    codec_if_ = es7210_codec_new(&codec_config);
    if (codec_if_ == nullptr) {
        return ESP_FAIL;
    }
    esp_codec_dev_sample_info_t sample{};
    sample.bits_per_sample = 16U;
    sample.channel = kSlots;
    sample.channel_mask = 0U;
    sample.sample_rate = config_.sample_rate;
    sample.mclk_multiple = 256U;
    if (codec_if_->set_fs(codec_if_, &sample) != ESP_CODEC_DEV_OK) {
        (void)audio_codec_delete_codec_if(codec_if_);
        codec_if_ = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

int32_t Es7210Microphone::Start(device::MicrophoneSampleSink sink, void* context, uint8_t gain_db) {
    return StartCapture(sink, nullptr, context, gain_db);
}

int32_t Es7210Microphone::StartWithReference(device::MicrophoneReferenceSink sink, void* context, uint8_t gain_db) {
    return StartCapture(nullptr, sink, context, gain_db);
}

int32_t Es7210Microphone::StartCapture(device::MicrophoneSampleSink sink, device::MicrophoneReferenceSink reference_sink,
                                       void* context, uint8_t gain_db) {
    if (task_ == nullptr || (sink == nullptr && reference_sink == nullptr)) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if (running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_STALE_STATE;
    }
    rx_ = clock_owner_.InputChannel();
    if (rx_ == nullptr) {
        ESP_LOGW(config_.log_tag, "no full-duplex RX channel; is the speaker codec up?");
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    const uint8_t gain = gain_db == kKeepDefaultGain ? config_.default_gain_db : gain_db;
    if (ConfigureCodec() != ESP_OK) {
        ESP_LOGE(config_.log_tag, "%s configuration failed", config_.name);
        return MICROPIXEL_STATUS_INTERNAL;
    }
    if (codec_if_->set_mic_gain(codec_if_, static_cast<float>(gain)) != ESP_CODEC_DEV_OK ||
        codec_if_->enable(codec_if_, true) != ESP_CODEC_DEV_OK) {
        return MICROPIXEL_STATUS_INTERNAL;
    }
    const esp_err_t enable_status = i2s_channel_enable(rx_);
    if (enable_status != ESP_OK && enable_status != ESP_ERR_INVALID_STATE) {
        (void)codec_if_->enable(codec_if_, false);
        return MICROPIXEL_STATUS_INTERNAL;
    }
    sink_ = sink;
    reference_sink_ = reference_sink;
    sink_context_ = context;
    (void)xSemaphoreTake(idle_, 0U);
    running_.store(true, std::memory_order_release);
    xTaskNotifyGive(task_);
    ESP_LOGI(config_.log_tag, "capture on: %u Hz, TDM slot 0 (MIC1), gain %u dB", static_cast<unsigned>(config_.sample_rate),
             static_cast<unsigned>(gain));
    return MICROPIXEL_STATUS_OK;
}

int32_t Es7210Microphone::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return MICROPIXEL_STATUS_OK;
    }
    // The task finishes its current read (bounded by kReadTimeoutMs) before
    // the sink may go away.
    const bool idle = xSemaphoreTake(idle_, pdMS_TO_TICKS(kStopTimeoutMs)) == pdTRUE;
    (void)i2s_channel_disable(rx_);
    (void)codec_if_->enable(codec_if_, false);
    sink_ = nullptr;
    reference_sink_ = nullptr;
    sink_context_ = nullptr;
    ESP_LOGI(config_.log_tag, "capture off");
    return idle ? MICROPIXEL_STATUS_OK : MICROPIXEL_STATUS_TIMEOUT;
}

void Es7210Microphone::TaskEntry(void* argument) { static_cast<Es7210Microphone*>(argument)->Run(); }

void Es7210Microphone::Run() {
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t frames_since_log = 0U;
        uint32_t logged_seconds = 0U;
        int32_t peak[kSlots]{};
        uint32_t zero_words = 0U;
        uint32_t total_words = 0U;
        while (running_.load(std::memory_order_acquire)) {
            size_t bytes_read = 0U;
            const esp_err_t status =
                i2s_channel_read(rx_, interleaved_, sizeof(interleaved_), &bytes_read, pdMS_TO_TICKS(kReadTimeoutMs));
            const uint32_t frames = static_cast<uint32_t>(bytes_read / (kSlots * sizeof(int16_t)));
            if (status != ESP_OK && status != ESP_ERR_TIMEOUT) {
                ESP_LOGW(config_.log_tag, "RX read failed: %s", esp_err_to_name(status));
                vTaskDelay(pdMS_TO_TICKS(kReadTimeoutMs));
                continue;
            }
            if (frames == 0U || !running_.load(std::memory_order_acquire)) {
                continue;
            }
            for (uint32_t word = 0U; word < frames * kSlots; ++word) {
                zero_words += interleaved_[word] == 0 ? 1U : 0U;
            }
            total_words += frames * kSlots;
            for (uint32_t frame = 0U; frame < frames; ++frame) {
                // Speech at arm's length reaches ~3,000 at 30-33 dB PGA; a
                // little extra keeps meters lively without clipping it.
                const int32_t boosted = static_cast<int32_t>(interleaved_[frame * kSlots]) * kTdmGain;
                mono_[frame] = static_cast<int16_t>(boosted > 32767 ? 32767 : (boosted < -32768 ? -32768 : boosted));
                // Slot 2 is the ES8311 output looped back: the echo reference.
                pairs_[frame * 2U] = mono_[frame];
                pairs_[frame * 2U + 1U] = interleaved_[frame * kSlots + kReferenceSlot];
                for (uint32_t slot = 0U; slot < kSlots; ++slot) {
                    const int32_t magnitude = std::abs(static_cast<int32_t>(interleaved_[frame * kSlots + slot]));
                    peak[slot] = magnitude > peak[slot] ? magnitude : peak[slot];
                }
            }
            if (reference_sink_ != nullptr) {
                reference_sink_(sink_context_, pairs_, frames);
            } else {
                sink_(sink_context_, mono_, frames);
            }
            frames_since_log += frames;
            if (frames_since_log >= config_.sample_rate) {
                if (logged_seconds < kLevelLogSeconds) {
                    ESP_LOGI(config_.log_tag, "peak MIC1=%ld MIC2=%ld MIC3=%ld MIC4=%ld zero words %lu/%lu",
                             static_cast<long>(peak[0]), static_cast<long>(peak[1]), static_cast<long>(peak[2]),
                             static_cast<long>(peak[3]), static_cast<unsigned long>(zero_words),
                             static_cast<unsigned long>(total_words));
                    ++logged_seconds;
                }
                frames_since_log = 0U;
                for (int32_t& slot_peak : peak) {
                    slot_peak = 0;
                }
                zero_words = 0U;
                total_words = 0U;
            }
        }
        (void)xSemaphoreGive(idle_);
    }
}

}  // namespace micropixel::platform::audio
