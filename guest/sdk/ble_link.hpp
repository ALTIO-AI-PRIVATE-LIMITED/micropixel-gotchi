#ifndef MICROPIXEL_SDK_BLE_LINK_HPP
#define MICROPIXEL_SDK_BLE_LINK_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

enum class BleLinkChannel : uint8_t {
    kReply = MICROPIXEL_BLE_LINK_CHANNEL_REPLY,
    kStatus = MICROPIXEL_BLE_LINK_CHANNEL_STATUS,
    kBulk = MICROPIXEL_BLE_LINK_CHANNEL_BULK,
};

struct BleLinkState final {
    bool available{};
    bool started{};
    bool connected{};
    bool encrypted{};
    // The peer subscribed to each notify channel.
    bool reply_subscribed{};
    bool status_subscribed{};
    bool bulk_subscribed{};
    uint16_t mtu{};
    // Payload bytes one BULK Notify() may carry.
    uint16_t bulk_payload{};
    uint8_t phy{};
    uint8_t peers{};
    uint32_t bulk_drops{};
    uint32_t reply_drops{};
    uint32_t bulk_kbps{};
};

struct BleLinkMessage final {
    uint32_t length{};
    // True for a write to the VALUE characteristic, false for INBOX.
    bool value{};
    // Messages still queued after this one.
    uint32_t pending{};
    // Messages the Host dropped before this Read() because its queue was full.
    uint32_t dropped{};
};

// An App's GATT service on the Host Bluetooth keyboard's connection, for a
// companion phone or computer app (UUIDs in micropixel_abi.h). Start() binds
// it to this App. BleLink inbox events say Read() has something; REPLY and
// STATUS notifications queue in the Host, BULK ones fail with kRateLimited
// while the radio is busy and a room event follows.
class BleLink final {
   public:
    static constexpr uint32_t kMaxMessage = MICROPIXEL_BLE_LINK_MAX_MESSAGE;
    static constexpr uint32_t kMaxValue = MICROPIXEL_BLE_LINK_MAX_VALUE;

    [[nodiscard]] Result<BleLinkState> State() const;
    [[nodiscard]] Result<void> Start() const;
    [[nodiscard]] Result<void> Stop() const;
    // Copies the oldest message into `data`; length 0 when the queue is empty.
    [[nodiscard]] Result<BleLinkMessage> Read(uint8_t (&data)[kMaxMessage]) const;
    // kInvalidState when the peer is not subscribed to `channel`; kRateLimited
    // for BULK while the radio is busy (wait for the room event).
    [[nodiscard]] Result<void> Notify(BleLinkChannel channel, const void* data, uint32_t length) const;
    // What reads of the VALUE characteristic return (encrypted link only).
    [[nodiscard]] Result<void> SetValue(const void* data, uint32_t length) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr BleLink(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
