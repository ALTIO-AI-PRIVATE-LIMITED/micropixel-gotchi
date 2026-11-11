#include "runtime/services/microphone_service.hpp"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "runtime/services/timer_service.hpp"

namespace micropixel::runtime {
namespace {

constexpr char kTag[] = "microphone";

}  // namespace

MicrophoneService::MicrophoneService(device::MicrophoneService& microphone, EventQueue& events, TimerService& clock)
    : microphone_(microphone),
      events_(events),
      clock_(clock),
      mutex_(xSemaphoreCreateMutex()),
      frame_samples_(microphone.SampleRate() * kFrameMilliseconds / 1000U) {}

MicrophoneService::~MicrophoneService() {
    Shutdown();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

micropixel_microphone_info_t MicrophoneService::Info() const {
    micropixel_microphone_info_t info{};
    info.size = sizeof(info);
    info.sample_rate = microphone_.SampleRate();
    info.frame_samples = static_cast<uint16_t>(frame_samples_);
    info.flags = (info.sample_rate != 0U ? static_cast<uint32_t>(MICROPIXEL_MICROPHONE_FLAG_AVAILABLE) : 0U) |
                 (started_ ? static_cast<uint32_t>(MICROPIXEL_MICROPHONE_FLAG_STARTED) : 0U);
    return info;
}

ServiceResult<void> MicrophoneService::Start(uint8_t gain_db) {
    if (!valid() || microphone_.SampleRate() == 0U) {
        return FailService<void>(MICROPIXEL_STATUS_UNSUPPORTED);
    }
    if (gain_db != 0xFFU && gain_db > 37U) {
        return FailService<void>(MICROPIXEL_STATUS_INVALID_ARGUMENT);
    }
    if (started_) {
        return FailService<void>(MICROPIXEL_STATUS_STALE_STATE);
    }
    if (ring_ == nullptr) {
        ring_ = static_cast<int16_t*>(
            heap_caps_calloc(kRingSamples, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (ring_ == nullptr) {
            return FailService<void>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
        }
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    read_position_ = 0U;
    write_position_ = 0U;
    dropped_ = 0U;
    event_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    gain_db_ = gain_db;
    const auto started = microphone_.Start(OnSamples, this, gain_db_);
    if (!started) {
        ESP_LOGW(kTag, "start failed: %d", static_cast<int>(started.error().status));
        return FailService<void>(started.error().status);
    }
    started_ = true;
    device_running_ = true;
    ESP_LOGI(kTag, "capture started: %u Hz, %u-sample frames", static_cast<unsigned>(microphone_.SampleRate()),
             static_cast<unsigned>(frame_samples_));
    return {};
}

void MicrophoneService::StopDevice() {
    if (device_running_) {
        (void)microphone_.Stop();
        device_running_ = false;
    }
}

ServiceResult<void> MicrophoneService::Stop() {
    if (!started_) {
        return {};
    }
    StopDevice();
    started_ = false;
    ESP_LOGI(kTag, "capture stopped");
    return {};
}

ServiceResult<micropixel_microphone_read_response_t> MicrophoneService::Read(int16_t* samples_out,
                                                                              uint32_t max_samples) {
    using Response = micropixel_microphone_read_response_t;
    if (!valid() || ring_ == nullptr) {
        Response empty{};
        empty.size = sizeof(empty);
        return empty;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t buffered = write_position_ - read_position_;
    const uint32_t count = std::min(buffered, max_samples);
    const uint32_t start = read_position_ % kRingSamples;
    const uint32_t first = std::min(count, kRingSamples - start);
    std::memcpy(samples_out, ring_ + start, first * sizeof(int16_t));
    std::memcpy(samples_out + first, ring_, (count - first) * sizeof(int16_t));
    read_position_ += count;
    Response response{};
    response.size = sizeof(response);
    response.sample_count = count;
    response.available_samples = buffered - count;
    response.dropped_samples = dropped_;
    dropped_ = 0U;
    event_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    return response;
}

void MicrophoneService::OnSamples(void* context, const int16_t* samples, uint32_t count) {
    static_cast<MicrophoneService*>(context)->HandleSamples(samples, count);
}

void MicrophoneService::HandleSamples(const int16_t* samples, uint32_t count) {
    if (count == 0U || ring_ == nullptr) {
        return;
    }
    if (count > kRingSamples) {
        samples += count - kRingSamples;
        count = kRingSamples;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t free_samples = kRingSamples - (write_position_ - read_position_);
    if (count > free_samples) {
        // Drop the oldest so what the Guest reads next stays recent.
        const uint32_t overflow = count - free_samples;
        read_position_ += overflow;
        dropped_ += overflow;
    }
    const uint32_t start = write_position_ % kRingSamples;
    const uint32_t first = std::min(count, kRingSamples - start);
    std::memcpy(ring_ + start, samples, first * sizeof(int16_t));
    std::memcpy(ring_, samples + first, (count - first) * sizeof(int16_t));
    write_position_ += count;
    const uint32_t buffered = write_position_ - read_position_;
    const uint32_t dropped = dropped_;
    const bool post = event_armed_ && buffered >= frame_samples_;
    if (post) {
        // Disarm before pushing: a READ racing the push re-arms it.
        event_armed_ = false;
    }
    (void)xSemaphoreGive(mutex_);
    if (!post) {
        return;
    }
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = MICROPIXEL_MICROPHONE_EVENT_FRAMES;
    event.service_id = MICROPIXEL_SERVICE_MICROPHONE;
    event.source = 1U;
    event.timestamp_us = clock_.Now();
    event.sequence = event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    micropixel_microphone_event_payload_t payload{};
    payload.available_samples = buffered;
    payload.dropped_samples = dropped;
    std::memcpy(event.payload, &payload, sizeof(payload));
    // Advisory: a full queue re-arms the event for the next chunk.
    if (!events_.PushAdvisory(event)) {
        (void)xSemaphoreTake(mutex_, portMAX_DELAY);
        event_armed_ = true;
        (void)xSemaphoreGive(mutex_);
    }
}

void MicrophoneService::Suspend() { StopDevice(); }

void MicrophoneService::Resume() {
    if (started_ && !device_running_) {
        device_running_ = static_cast<bool>(microphone_.Start(OnSamples, this, gain_db_));
    }
}

void MicrophoneService::Shutdown() {
    StopDevice();
    started_ = false;
    if (ring_ != nullptr) {
        heap_caps_free(ring_);
        ring_ = nullptr;
    }
}

}  // namespace micropixel::runtime
