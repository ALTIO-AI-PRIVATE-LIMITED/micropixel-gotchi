#include "runtime/services/peer_radio_service.hpp"

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

PeerRadioService::PeerRadioService(device::PeerRadioService& radio, EventQueue& events, TimerService& clock)
    : radio_(radio), events_(events), clock_(clock), mutex_(xSemaphoreCreateMutex()) {}

PeerRadioService::~PeerRadioService() {
    Shutdown();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

ServiceResult<void> PeerRadioService::Start() {
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
    packet_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    const device::PeerRadioCallbacks callbacks{
        .packet = OnPacket, .state = OnState, .candidate = OnCandidate, .context = this};
    auto started = radio_.Start(callbacks);
    if (!started) {
        return FailService<void>(started.error().status);
    }
    started_ = true;
    return {};
}

ServiceResult<void> PeerRadioService::Stop() {
    if (!started_) {
        return {};
    }
    started_ = false;
    return Forward(radio_.Stop());
}

ServiceResult<void> PeerRadioService::BeginPairing() { return Forward(radio_.BeginPairing()); }

ServiceResult<void> PeerRadioService::AcceptPeer(const uint8_t (&address)[6]) {
    return Forward(radio_.AcceptPeer(address));
}

ServiceResult<void> PeerRadioService::Forget() { return Forward(radio_.Forget()); }

ServiceResult<void> PeerRadioService::Send(const uint8_t* data, uint32_t length) {
    return Forward(radio_.Send(data, length));
}

ServiceResult<micropixel_peer_radio_receive_response_t> PeerRadioService::Receive(uint8_t* payload_out,
                                                                                   uint32_t capacity) {
    micropixel_peer_radio_receive_response_t response{};
    response.size = sizeof(response);
    if (!valid() || slots_ == nullptr) {
        return response;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    if (count_ != 0U) {
        const Slot& slot = slots_[head_];
        const uint32_t length = std::min<uint32_t>(slot.length, capacity);
        std::memcpy(payload_out, slot.data, length);
        response.length = static_cast<uint16_t>(length);
        response.rssi = slot.rssi;
        head_ = (head_ + 1U) % kQueueSlots;
        --count_;
    }
    response.pending = static_cast<uint16_t>(count_);
    response.dropped = dropped_;
    dropped_ = 0U;
    packet_armed_ = true;
    (void)xSemaphoreGive(mutex_);
    return response;
}

void PeerRadioService::Shutdown() {
    if (started_) {
        started_ = false;
        (void)radio_.Stop();
    }
    if (slots_ != nullptr) {
        heap_caps_free(slots_);
        slots_ = nullptr;
    }
}

void PeerRadioService::Post(uint16_t event_id, const void* payload, uint32_t payload_size) {
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = event_id;
    event.service_id = MICROPIXEL_SERVICE_PEER_RADIO;
    event.source = 1U;
    event.timestamp_us = clock_.Now();
    event.sequence = event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    std::memcpy(event.payload, payload, payload_size);
    if (event_id != MICROPIXEL_PEER_RADIO_EVENT_PACKET) {
        (void)events_.PushAdvisory(event);
        return;
    }
    // A full queue re-arms PACKET for the next arrival.
    if (!events_.PushAdvisory(event)) {
        (void)xSemaphoreTake(mutex_, portMAX_DELAY);
        packet_armed_ = true;
        (void)xSemaphoreGive(mutex_);
    }
}

void PeerRadioService::OnPacket(void* context, const uint8_t* data, uint32_t length, int8_t rssi) {
    auto& self = *static_cast<PeerRadioService*>(context);
    if (self.slots_ == nullptr || length > MICROPIXEL_PEER_RADIO_MAX_PAYLOAD) {
        return;
    }
    (void)xSemaphoreTake(self.mutex_, portMAX_DELAY);
    if (self.count_ == kQueueSlots) {
        // Drop the oldest: late audio is worth less than fresh audio.
        self.head_ = (self.head_ + 1U) % kQueueSlots;
        --self.count_;
        ++self.dropped_;
    }
    Slot& slot = self.slots_[(self.head_ + self.count_) % kQueueSlots];
    slot.length = static_cast<uint16_t>(length);
    slot.rssi = rssi;
    std::memcpy(slot.data, data, length);
    ++self.count_;
    const bool post = self.packet_armed_;
    self.packet_armed_ = false;
    const uint32_t pending = self.count_;
    (void)xSemaphoreGive(self.mutex_);
    if (post) {
        micropixel_peer_radio_packet_event_payload_t payload{};
        payload.pending = pending;
        self.Post(MICROPIXEL_PEER_RADIO_EVENT_PACKET, &payload, sizeof(payload));
    }
}

void PeerRadioService::OnState(void* context, uint32_t flags, uint8_t channel, int8_t rssi) {
    micropixel_peer_radio_state_event_payload_t payload{};
    payload.flags = flags;
    payload.channel = channel;
    payload.peer_rssi = rssi;
    static_cast<PeerRadioService*>(context)->Post(MICROPIXEL_PEER_RADIO_EVENT_STATE, &payload, sizeof(payload));
}

void PeerRadioService::OnCandidate(void* context, const uint8_t (&address)[6], uint16_t code, int8_t rssi) {
    micropixel_peer_radio_candidate_event_payload_t payload{};
    std::memcpy(payload.address, address, sizeof(payload.address));
    payload.code = code;
    payload.rssi = rssi;
    static_cast<PeerRadioService*>(context)->Post(MICROPIXEL_PEER_RADIO_EVENT_PAIR_CANDIDATE, &payload,
                                                  sizeof(payload));
}

}  // namespace micropixel::runtime
