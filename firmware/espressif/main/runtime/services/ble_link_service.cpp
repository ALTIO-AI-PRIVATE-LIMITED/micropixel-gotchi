#include "runtime/services/ble_link_service.hpp"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "runtime/services/timer_service.hpp"

namespace micropixel::runtime {
namespace {

template <typename Result>
ServiceResult<void> Forward(const Result& result) {
    return result ? ServiceResult<void>{} : FailService<void>(result.error().status);
}

}  // namespace

BleLinkService::BleLinkService(device::BleLinkService& link, EventQueue& events, TimerService& clock)
    : link_(link), events_(events), clock_(clock), mutex_(xSemaphoreCreateMutex()) {}

BleLinkService::~BleLinkService() {
    Shutdown();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

ServiceResult<void> BleLinkService::Start() {
    if (!valid()) {
        return FailService<void>(MICROPIXEL_STATUS_UNSUPPORTED);
    }
    if (started_) {
        return {};
    }
    if (slots_ == nullptr) {
        slots_ = static_cast<Slot*>(heap_caps_calloc(kQueueSlots, sizeof(Slot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (slots_ == nullptr) {
            return FailService<void>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
        }
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    head_ = 0U;
    count_ = 0U;
    dropped_ = 0U;
    inbox_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    const device::BleLinkCallbacks callbacks{.message = OnMessage, .state = OnState, .room = OnRoom, .context = this};
    auto started = link_.Start(callbacks);
    if (!started) {
        return FailService<void>(started.error().status);
    }
    started_ = true;
    return {};
}

ServiceResult<void> BleLinkService::Stop() {
    if (!started_) {
        return {};
    }
    started_ = false;
    return Forward(link_.Stop());
}

ServiceResult<micropixel_ble_link_read_response_t> BleLinkService::Read(uint8_t* data_out, uint32_t capacity) {
    micropixel_ble_link_read_response_t response{};
    response.size = sizeof(response);
    if (!valid() || slots_ == nullptr) {
        return response;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    if (count_ != 0U) {
        const Slot& slot = slots_[head_];
        const uint32_t length = std::min<uint32_t>(slot.length, capacity);
        std::memcpy(data_out, slot.data, length);
        response.length = static_cast<uint16_t>(length);
        response.source = slot.source;
        head_ = (head_ + 1U) % kQueueSlots;
        --count_;
    }
    response.pending = static_cast<uint16_t>(count_);
    response.dropped = dropped_;
    dropped_ = 0U;
    inbox_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    return response;
}

ServiceResult<void> BleLinkService::Notify(uint8_t channel, const uint8_t* data, uint32_t length) {
    if (!started_) {
        return FailService<void>(MICROPIXEL_STATUS_CLOSED);
    }
    if (channel == MICROPIXEL_BLE_LINK_CHANNEL_BULK) {
        return Forward(link_.SendBulk(data, length));
    }
    return Forward(link_.Queue(channel, data, length));
}

ServiceResult<void> BleLinkService::SetValue(const uint8_t* data, uint32_t length) {
    return Forward(link_.SetValue(data, length));
}

void BleLinkService::Shutdown() {
    if (started_) {
        started_ = false;
        (void)link_.Stop();
    }
    if (slots_ != nullptr) {
        heap_caps_free(slots_);
        slots_ = nullptr;
    }
}

void BleLinkService::Post(uint16_t event_id, uint32_t flags, uint32_t pending) {
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = event_id;
    event.service_id = MICROPIXEL_SERVICE_BLE_LINK;
    event.source = 1U;
    event.timestamp_us = clock_.Now();
    event.sequence = event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    micropixel_ble_link_event_payload_t payload{};
    payload.flags = flags;
    payload.pending = pending;
    std::memcpy(event.payload, &payload, sizeof(payload));
    if (!events_.PushAdvisory(event) && event_id == MICROPIXEL_BLE_LINK_EVENT_INBOX) {
        // A full queue re-arms INBOX for the next message.
        (void)xSemaphoreTake(mutex_, portMAX_DELAY);
        inbox_armed_ = true;
        (void)xSemaphoreGive(mutex_);
    }
}

void BleLinkService::OnMessage(void* context, uint8_t source, const uint8_t* data, uint32_t length) {
    auto& self = *static_cast<BleLinkService*>(context);
    if (self.slots_ == nullptr || length > MICROPIXEL_BLE_LINK_MAX_MESSAGE) {
        return;
    }
    (void)xSemaphoreTake(self.mutex_, portMAX_DELAY);
    if (self.count_ == kQueueSlots) {
        // Commands are retried by the peer on timeout: keep the older ones in order.
        ++self.dropped_;
        (void)xSemaphoreGive(self.mutex_);
        return;
    }
    Slot& slot = self.slots_[(self.head_ + self.count_) % kQueueSlots];
    slot.length = static_cast<uint16_t>(length);
    slot.source = source;
    std::memcpy(slot.data, data, length);
    ++self.count_;
    const bool post = self.inbox_armed_;
    self.inbox_armed_ = false;
    const uint32_t pending = self.count_;
    (void)xSemaphoreGive(self.mutex_);
    if (post) {
        self.Post(MICROPIXEL_BLE_LINK_EVENT_INBOX, 0U, pending);
    }
}

void BleLinkService::OnState(void* context, uint32_t flags) {
    static_cast<BleLinkService*>(context)->Post(MICROPIXEL_BLE_LINK_EVENT_STATE, flags, 0U);
}

void BleLinkService::OnRoom(void* context) {
    static_cast<BleLinkService*>(context)->Post(MICROPIXEL_BLE_LINK_EVENT_ROOM, 0U, 0U);
}

}  // namespace micropixel::runtime
