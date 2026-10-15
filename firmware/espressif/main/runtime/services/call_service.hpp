#ifndef MICROPIXEL_RUNTIME_SERVICES_CALL_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_CALL_SERVICE_HPP

#include <atomic>
#include <cstdint>

#include "abi/micropixel_abi.h"
#include "device/device_services.hpp"
#include "runtime/event_queue.hpp"
#include "runtime/services/service_result.hpp"

namespace micropixel::runtime {

class TimerService;

// Call 1.1: DIAL and HANG_UP return at once; progress arrives as advisory
// STATE events and the Guest can always re-read GET_STATE. A call still
// running when the session ends is hung up.
class CallService final {
   public:
    CallService(device::CallService& call, EventQueue& events, TimerService& clock)
        : call_(call), events_(events), clock_(clock) {}
    CallService(const CallService&) = delete;
    CallService& operator=(const CallService&) = delete;
    ~CallService() { Shutdown(); }

    [[nodiscard]] micropixel_call_state_t State() const { return call_.State(); }
    [[nodiscard]] ServiceResult<void> Dial(const char* callee);
    [[nodiscard]] ServiceResult<void> HangUp();
    [[nodiscard]] ServiceResult<void> Meetings(micropixel_call_meetings_t& meetings_out);
    [[nodiscard]] ServiceResult<void> SetMuted(bool muted);
    void Shutdown();

   private:
    static void OnState(void* context, uint8_t phase, uint8_t end_reason);

    device::CallService& call_;
    EventQueue& events_;
    TimerService& clock_;
    bool dialed_{};
    std::atomic<uint32_t> event_sequence_{0U};
};

}  // namespace micropixel::runtime

#endif
