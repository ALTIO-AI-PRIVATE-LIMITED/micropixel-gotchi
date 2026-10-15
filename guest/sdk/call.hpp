#ifndef MICROPIXEL_SDK_CALL_HPP
#define MICROPIXEL_SDK_CALL_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

enum class CallPhase : uint8_t {
    kIdle = MICROPIXEL_CALL_PHASE_IDLE,
    kDialing = MICROPIXEL_CALL_PHASE_DIALING,
    kRinging = MICROPIXEL_CALL_PHASE_RINGING,
    kConnected = MICROPIXEL_CALL_PHASE_CONNECTED,
};

enum class CallEndReason : uint8_t {
    kNone = MICROPIXEL_CALL_END_NONE,
    kHungUp = MICROPIXEL_CALL_END_HUNG_UP,
    kRemoteHungUp = MICROPIXEL_CALL_END_REMOTE_HUNG_UP,
    kNoAnswer = MICROPIXEL_CALL_END_NO_ANSWER,
    kFailed = MICROPIXEL_CALL_END_FAILED,
};

struct CallState final {
    // The device can place calls (call server configured).
    bool available{};
    CallPhase phase{CallPhase::kIdle};
    // Why the last call ended; kNone before the first one.
    CallEndReason end_reason{CallEndReason::kNone};
    uint32_t connected_ms{};
};

// Voice call to a phone through the device's call server. Dial() returns at
// once; Call events report each phase change. The speaker and microphones
// belong to the call while it runs. Leaving the App hangs up.
class Call final {
   public:
    static constexpr uint32_t kMaxCalleeBytes = MICROPIXEL_CALL_MAX_CALLEE_BYTES;

    [[nodiscard]] Result<CallState> State() const;
    // kUnsupported when calling is not set up; kResourceExhausted while a call runs.
    [[nodiscard]] Result<void> Dial(const char* callee) const;
    [[nodiscard]] Result<void> HangUp() const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr Call(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
