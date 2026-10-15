#ifndef MICROPIXEL_DEVICE_CALL_HPP
#define MICROPIXEL_DEVICE_CALL_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Called on the call task on every phase change; must not block.
using CallStateSink = void (*)(void* context, uint8_t phase, uint8_t end_reason);

// Voice call to a phone through the board's call server. Dial() and HangUp()
// return at once; progress arrives through the sink, ending with IDLE.
class Call {
   public:
    virtual ~Call() = default;
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;

    [[nodiscard]] virtual micropixel_call_state_t State() const = 0;
    // kInvalidState while a call is in progress; kUnsupported when unavailable.
    [[nodiscard]] virtual int32_t Dial(const char* callee, CallStateSink sink, void* context) = 0;
    [[nodiscard]] virtual int32_t HangUp() = 0;
    // Hangs up and stops calling the sink before it returns.
    virtual void Release() = 0;

   protected:
    Call() = default;
};

}  // namespace micropixel::device

#endif
