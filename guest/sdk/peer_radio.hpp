#ifndef MICROPIXEL_SDK_PEER_RADIO_HPP
#define MICROPIXEL_SDK_PEER_RADIO_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

using PeerAddress = uint8_t[6];

struct PeerRadioState final {
    bool available{};
    bool started{};
    bool paired{};
    // The paired device was heard within the last 2 s.
    bool peer_present{};
    bool pairing{};
    // Pinned to the channel of the Wi-Fi network this device is joined to.
    bool on_wifi_channel{};
    uint8_t channel{};
    int8_t peer_rssi{};
    uint8_t self_address[6]{};
    uint8_t peer_address[6]{};
    uint32_t packets_sent{};
    uint32_t packets_received{};
    uint32_t send_failures{};
};

struct PeerPacket final {
    uint32_t length{};
    int8_t rssi{};
    // Packets still queued after this one.
    uint32_t pending{};
    // Packets the Host dropped before this Receive() because the queue was full.
    uint32_t dropped{};
};

// Short-range link to one paired device of the same kind (ESP-NOW). Start()
// brings the radio up for this App, turning Wi-Fi on if needed. Pairing:
// both devices call BeginPairing(); each sees the other as a PeerRadio
// candidate event with a 4-digit code shown on both screens; Accept() it on
// both. The pairing is saved on the device. Send() is best effort (no
// retries); PeerRadio packet events say Receive() has something.
class PeerRadio final {
   public:
    static constexpr uint32_t kMaxPayload = MICROPIXEL_PEER_RADIO_MAX_PAYLOAD;

    [[nodiscard]] Result<PeerRadioState> State() const;
    [[nodiscard]] Result<void> Start() const;
    [[nodiscard]] Result<void> Stop() const;
    [[nodiscard]] Result<void> BeginPairing() const;
    [[nodiscard]] Result<void> Accept(const uint8_t (&address)[6]) const;
    [[nodiscard]] Result<void> Forget() const;
    // kNotFound when nothing is paired; kRateLimited when the radio is busy.
    [[nodiscard]] Result<void> Send(const void* data, uint32_t length) const;
    // Copies the oldest packet into `data` (kMaxPayload bytes); length 0 when
    // the queue is empty.
    [[nodiscard]] Result<PeerPacket> Receive(uint8_t (&data)[kMaxPayload]) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr PeerRadio(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
