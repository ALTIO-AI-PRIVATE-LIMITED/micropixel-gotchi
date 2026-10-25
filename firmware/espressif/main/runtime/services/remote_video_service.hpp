#ifndef MICROPIXEL_RUNTIME_SERVICES_REMOTE_VIDEO_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_REMOTE_VIDEO_SERVICE_HPP

#include <atomic>
#include <cstdint>

#include "abi/micropixel_abi.h"
#include "device/device_services.hpp"
#include "runtime/event_queue.hpp"
#include "runtime/services/service_result.hpp"

namespace micropixel::runtime {

class TimerService;

// Remote Video 1.0: the receiver decodes frames on its own task; the Guest
// copies the newest one with READ_FRAME. FRAME is advisory and fires once per
// read cycle, so a slow Guest never floods its queue and simply skips frames.
class RemoteVideoService final {
   public:
    RemoteVideoService(device::RemoteVideoService& video, EventQueue& events, TimerService& clock)
        : video_(video), events_(events), clock_(clock) {}
    RemoteVideoService(const RemoteVideoService&) = delete;
    RemoteVideoService& operator=(const RemoteVideoService&) = delete;
    ~RemoteVideoService() { Shutdown(); }

    [[nodiscard]] micropixel_remote_video_state_t State() const { return video_.State(); }
    [[nodiscard]] ServiceResult<void> Start();
    [[nodiscard]] ServiceResult<void> Stop();
    // Writes `width * height` pixels to `pixels` (Guest memory).
    [[nodiscard]] ServiceResult<void> ReadFrame(uint16_t* pixels, uint16_t width, uint16_t height, bool byte_swapped);
    void Suspend();
    void Resume();
    void Shutdown();

   private:
    static void OnFrame(void* context, uint32_t frames_received, uint16_t width, uint16_t height);
    void StopDevice();

    device::RemoteVideoService& video_;
    EventQueue& events_;
    TimerService& clock_;
    std::atomic<bool> event_armed_{false};
    std::atomic<uint32_t> event_sequence_{0U};
    // Guest-task state.
    bool started_{};
    bool device_running_{};
};

}  // namespace micropixel::runtime

#endif
