#ifndef MICROPIXEL_DEVICE_BLE_LINK_HPP
#define MICROPIXEL_DEVICE_BLE_LINK_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Delivered on the Bluetooth host or link task; none may block for long.
struct BleLinkCallbacks final {
    // `source` is MICROPIXEL_BLE_LINK_SOURCE_*.
    void (*message)(void* context, uint8_t source, const uint8_t* data, uint32_t length){};
    void (*state)(void* context, uint32_t flags){};
    // BULK can send again after a RATE_LIMITED SendBulk().
    void (*room)(void* context){};
    void* context{};
};

// An App's GATT service on the Host keyboard's LE connection (BLE Link in
// micropixel_abi.h). The service is registered with the keyboard at boot;
// Start() binds one consumer until Stop().
class BleLink {
   public:
    virtual ~BleLink() = default;
    BleLink(const BleLink&) = delete;
    BleLink& operator=(const BleLink&) = delete;

    [[nodiscard]] virtual micropixel_ble_link_state_t State() const = 0;
    [[nodiscard]] virtual int32_t Start(const BleLinkCallbacks& callbacks) = 0;
    // Returns once no callback will run any more.
    [[nodiscard]] virtual int32_t Stop() = 0;
    // REPLY or STATUS: queued, sent before further BULK data.
    [[nodiscard]] virtual int32_t Queue(uint8_t channel, const uint8_t* data, uint32_t length) = 0;
    // RATE_LIMITED while the controller is busy; room() follows.
    [[nodiscard]] virtual int32_t SendBulk(const uint8_t* data, uint32_t length) = 0;
    [[nodiscard]] virtual int32_t SetValue(const uint8_t* data, uint32_t length) = 0;

   protected:
    BleLink() = default;
};

}  // namespace micropixel::device

#endif
