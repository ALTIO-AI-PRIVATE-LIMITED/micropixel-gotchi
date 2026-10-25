#include "runtime/services/remote_video_service.hpp"

#include <cstring>

#include "esp_log.h"
#include "runtime/services/timer_service.hpp"

namespace micropixel::runtime {
namespace {

constexpr char kTag[] = "remote_video";

}  // namespace

ServiceResult<void> RemoteVideoService::Start() {
    if (started_) {
        return FailService<void>(MICROPIXEL_STATUS_STALE_STATE);
    }
    event_armed_.store(true, std::memory_order_relaxed);
    const auto result = video_.Start(OnFrame, this);
    if (!result) {
        ESP_LOGW(kTag, "start failed: %d", static_cast<int>(result.error().status));
        return FailService<void>(result.error().status);
    }
    started_ = true;
    device_running_ = true;
    return {};
}

void RemoteVideoService::StopDevice() {
    if (device_running_) {
        (void)video_.Stop();
        device_running_ = false;
    }
}

ServiceResult<void> RemoteVideoService::Stop() {
    StopDevice();
    started_ = false;
    return {};
}

ServiceResult<void> RemoteVideoService::ReadFrame(uint16_t* pixels, uint16_t width, uint16_t height,
                                                  bool byte_swapped) {
    if (!started_) {
        return FailService<void>(MICROPIXEL_STATUS_STALE_STATE);
    }
    // Re-arm first: a frame landing during the copy still gets its event.
    event_armed_.store(true, std::memory_order_relaxed);
    const auto result = video_.ReadFrame(pixels, width, height, byte_swapped);
    return result ? ServiceResult<void>{} : FailService<void>(result.error().status);
}

void RemoteVideoService::OnFrame(void* context, uint32_t frames_received, uint16_t width, uint16_t height) {
    auto& self = *static_cast<RemoteVideoService*>(context);
    if (!self.event_armed_.exchange(false, std::memory_order_relaxed)) {
        return;
    }
    micropixel_remote_video_frame_event_payload_t payload{};
    payload.frames_received = frames_received;
    payload.frame_width = width;
    payload.frame_height = height;
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = MICROPIXEL_REMOTE_VIDEO_EVENT_FRAME;
    event.service_id = MICROPIXEL_SERVICE_REMOTE_VIDEO;
    event.source = 1U;
    event.timestamp_us = self.clock_.Now();
    event.sequence = self.event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    std::memcpy(event.payload, &payload, sizeof(payload));
    // Advisory: a full queue re-arms the event for the next frame.
    if (!self.events_.PushAdvisory(event)) {
        self.event_armed_.store(true, std::memory_order_relaxed);
    }
}

void RemoteVideoService::Suspend() { StopDevice(); }

void RemoteVideoService::Resume() {
    if (started_ && !device_running_) {
        event_armed_.store(true, std::memory_order_relaxed);
        device_running_ = static_cast<bool>(video_.Start(OnFrame, this));
    }
}

void RemoteVideoService::Shutdown() {
    StopDevice();
    started_ = false;
}

}  // namespace micropixel::runtime
