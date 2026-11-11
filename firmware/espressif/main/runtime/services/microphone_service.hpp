#ifndef MICROPIXEL_RUNTIME_SERVICES_MICROPHONE_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_MICROPHONE_SERVICE_HPP

#include <atomic>
#include <cstdint>

#include "abi/micropixel_abi.h"
#include "device/device_services.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "runtime/event_queue.hpp"
#include "runtime/services/service_result.hpp"

namespace micropixel::runtime {

class TimerService;

// Microphone 1.0: the capture task pushes mono samples into a PSRAM ring and
// the Guest pulls them with READ. FRAMES is advisory and fires once per read
// cycle when a frame is buffered, so a slow Guest never floods its queue.
// When the ring is full the oldest samples go, keeping latency bounded.
class MicrophoneService final {
   public:
    // 20 ms at 16 kHz; the walkie-talkie and speech codecs work in this unit.
    static constexpr uint32_t kFrameMilliseconds = 20U;
    // About 0.5 s at 16 kHz; must be a power of two.
    static constexpr uint32_t kRingSamples = 8192U;

    MicrophoneService(device::MicrophoneService& microphone, EventQueue& events, TimerService& clock);
    MicrophoneService(const MicrophoneService&) = delete;
    MicrophoneService& operator=(const MicrophoneService&) = delete;
    ~MicrophoneService();

    [[nodiscard]] bool valid() const { return mutex_ != nullptr; }  // NOLINT(readability-identifier-naming)
    [[nodiscard]] micropixel_microphone_info_t Info() const;
    [[nodiscard]] ServiceResult<void> Start(uint8_t gain_db);
    [[nodiscard]] ServiceResult<void> Stop();
    // Copies up to `max_samples` into `samples_out` (Guest memory).
    [[nodiscard]] ServiceResult<micropixel_microphone_read_response_t> Read(int16_t* samples_out,
                                                                             uint32_t max_samples);
    void Suspend();
    void Resume();
    void Shutdown();

   private:
    static void OnSamples(void* context, const int16_t* samples, uint32_t count);
    void HandleSamples(const int16_t* samples, uint32_t count);
    void StopDevice();

    device::MicrophoneService& microphone_;
    EventQueue& events_;
    TimerService& clock_;
    SemaphoreHandle_t mutex_{};
    int16_t* ring_{};
    uint32_t frame_samples_{};
    // Guarded by mutex_.
    uint32_t read_position_{};
    uint32_t write_position_{};
    uint32_t dropped_{};
    bool event_armed_{};
    // Guest-task state.
    bool started_{};
    bool device_running_{};
    uint8_t gain_db_{0xFFU};
    std::atomic<uint32_t> event_sequence_{0U};
};

}  // namespace micropixel::runtime

#endif
