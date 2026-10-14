#ifndef MICROPIXEL_RUNTIME_SERVICES_PEER_RADIO_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_PEER_RADIO_SERVICE_HPP

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

// Peer Radio 1.0: packets from the paired device queue in PSRAM until the
// Guest RECEIVEs them; PACKET is advisory and re-armed by RECEIVE, like the
// microphone. STATE and PAIR_CANDIDATE are advisory too: the Guest can always
// re-read GET_STATE. The radio stops with the session.
class PeerRadioService final {
   public:
    // 320 ms of 20 ms walkie-talkie frames.
    static constexpr uint32_t kQueueSlots = 16U;

    PeerRadioService(device::PeerRadioService& radio, EventQueue& events, TimerService& clock);
    PeerRadioService(const PeerRadioService&) = delete;
    PeerRadioService& operator=(const PeerRadioService&) = delete;
    ~PeerRadioService();

    [[nodiscard]] bool valid() const { return mutex_ != nullptr; }  // NOLINT(readability-identifier-naming)
    [[nodiscard]] micropixel_peer_radio_state_t State() const { return radio_.State(); }
    [[nodiscard]] ServiceResult<void> Start();
    [[nodiscard]] ServiceResult<void> Stop();
    [[nodiscard]] ServiceResult<void> BeginPairing();
    [[nodiscard]] ServiceResult<void> AcceptPeer(const uint8_t (&address)[6]);
    [[nodiscard]] ServiceResult<void> Forget();
    [[nodiscard]] ServiceResult<void> Send(const uint8_t* data, uint32_t length);
    // Copies the oldest packet into `payload_out` (Guest memory, `capacity`).
    [[nodiscard]] ServiceResult<micropixel_peer_radio_receive_response_t> Receive(uint8_t* payload_out,
                                                                                   uint32_t capacity);
    void Shutdown();

   private:
    struct Slot final {
        uint16_t length{};
        int8_t rssi{};
        uint8_t data[MICROPIXEL_PEER_RADIO_MAX_PAYLOAD]{};
    };

    static void OnPacket(void* context, const uint8_t* data, uint32_t length, int8_t rssi);
    static void OnState(void* context, uint32_t flags, uint8_t channel, int8_t rssi);
    static void OnCandidate(void* context, const uint8_t (&address)[6], uint16_t code, int8_t rssi);
    void Post(uint16_t event_id, const void* payload, uint32_t payload_size);

    device::PeerRadioService& radio_;
    EventQueue& events_;
    TimerService& clock_;
    SemaphoreHandle_t mutex_{};
    Slot* slots_{};
    // Guarded by mutex_.
    uint32_t head_{};
    uint32_t count_{};
    uint32_t dropped_{};
    bool packet_armed_{};
    bool started_{};
    std::atomic<uint32_t> event_sequence_{0U};
};

}  // namespace micropixel::runtime

#endif
