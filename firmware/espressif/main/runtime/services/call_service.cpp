#include "runtime/services/call_service.hpp"

#include <cstring>

#include "runtime/services/timer_service.hpp"

namespace micropixel::runtime {

ServiceResult<void> CallService::Dial(const char* callee) {
    auto result = call_.Dial(callee, OnState, this);
    if (!result) {
        return FailService<void>(result.error().status);
    }
    dialed_ = true;
    return {};
}

ServiceResult<void> CallService::HangUp() {
    auto result = call_.HangUp();
    return result ? ServiceResult<void>{} : FailService<void>(result.error().status);
}

ServiceResult<void> CallService::Meetings(micropixel_call_meetings_t& meetings_out) {
    auto result = call_.Meetings(meetings_out);
    return result ? ServiceResult<void>{} : FailService<void>(result.error().status);
}

ServiceResult<void> CallService::SetMuted(bool muted) {
    auto result = call_.SetMuted(muted);
    return result ? ServiceResult<void>{} : FailService<void>(result.error().status);
}

void CallService::Shutdown() {
    if (dialed_) {
        dialed_ = false;
        call_.Release();
    }
}

void CallService::OnState(void* context, uint8_t phase, uint8_t end_reason) {
    auto& self = *static_cast<CallService*>(context);
    micropixel_call_state_event_payload_t payload{};
    payload.phase = phase;
    payload.end_reason = end_reason;
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = MICROPIXEL_CALL_EVENT_STATE;
    event.service_id = MICROPIXEL_SERVICE_CALL;
    event.source = 1U;
    event.timestamp_us = self.clock_.Now();
    event.sequence = self.event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)self.events_.PushAdvisory(event);
}

}  // namespace micropixel::runtime
