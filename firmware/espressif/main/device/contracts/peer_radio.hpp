#ifndef MICROPIXEL_DEVICE_PEER_RADIO_HPP
#define MICROPIXEL_DEVICE_PEER_RADIO_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Delivered on the radio task; none may block for long.
struct PeerRadioCallbacks final {
    void (*packet)(void* context, const uint8_t* data, uint32_t length, int8_t rssi){};
    void (*state)(void* context, uint32_t flags, uint8_t channel, int8_t rssi){};
    void (*candidate)(void* context, const uint8_t (&address)[6], uint16_t code, int8_t rssi){};
    void* context{};
};

// Point-to-point link to one paired device. Start() binds the consumer until
// Stop(); the pairing survives both and Host restarts.
class PeerRadio {
   public:
    virtual ~PeerRadio() = default;
    PeerRadio(const PeerRadio&) = delete;
    PeerRadio& operator=(const PeerRadio&) = delete;

    [[nodiscard]] virtual micropixel_peer_radio_state_t State() const = 0;
    [[nodiscard]] virtual int32_t Start(const PeerRadioCallbacks& callbacks) = 0;
    // Returns once no callback will run any more.
    [[nodiscard]] virtual int32_t Stop() = 0;
    [[nodiscard]] virtual int32_t BeginPairing() = 0;
    [[nodiscard]] virtual int32_t AcceptPeer(const uint8_t (&address)[6]) = 0;
    [[nodiscard]] virtual int32_t Forget() = 0;
    [[nodiscard]] virtual int32_t Send(const uint8_t* data, uint32_t length) = 0;

   protected:
    PeerRadio() = default;
};

}  // namespace micropixel::device

#endif
