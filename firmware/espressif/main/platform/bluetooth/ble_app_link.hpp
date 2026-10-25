#ifndef MICROPIXEL_PLATFORM_BLUETOOTH_BLE_APP_LINK_HPP
#define MICROPIXEL_PLATFORM_BLUETOOTH_BLE_APP_LINK_HPP

#include <atomic>
#include <cstdint>

#include "device/contracts/ble_link.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace micropixel::platform::bluetooth {

// The BLE Link GATT service, served on the keyboard's NimBLE host. The
// keyboard calls RegisterServices() between nimble_port_init() and starting
// the host task. Send pacing follows ESP32 iPhone Agent's ble_link.cpp
// (MIT, Henry Yuan): REPLY and STATUS go through one TX task and always go
// out before more BULK data, and BULK keeps only a few packets queued in the
// host while messages flow, so a reply never waits behind much bulk. (That
// firmware counted controller ACL credits; this NimBLE build keeps them
// private, so the backlog is measured in queued msys blocks instead.)
class BleAppLink final : public device::BleLink {
   public:
    BleAppLink() = default;

    [[nodiscard]] micropixel_ble_link_state_t State() const override;
    [[nodiscard]] int32_t Start(const device::BleLinkCallbacks& callbacks) override;
    [[nodiscard]] int32_t Stop() override;
    [[nodiscard]] int32_t Queue(uint8_t channel, const uint8_t* data, uint32_t length) override;
    [[nodiscard]] int32_t SendBulk(const uint8_t* data, uint32_t length) override;
    [[nodiscard]] int32_t SetValue(const uint8_t* data, uint32_t length) override;

    // Keyboard bring-up hook; not part of the device contract.
    [[nodiscard]] bool RegisterServices();

    // NimBLE callbacks.
    int Access(uint16_t connection, uint16_t attribute, void* context);
    int OnGapEvent(void* event);

   private:
    struct Text final {
        uint16_t length{};
        uint8_t data[MICROPIXEL_BLE_LINK_MAX_MESSAGE]{};
    };

    static void TaskEntry(void* context);
    void Task();
    void SendText(uint8_t channel, const Text& text, uint32_t timeout_ms);
    [[nodiscard]] bool NotifyOne(uint8_t channel, const uint8_t* data, uint32_t length, uint32_t timeout_ms);
    [[nodiscard]] bool BulkRoom(uint32_t length) const;
    [[nodiscard]] uint16_t Mtu() const;
    [[nodiscard]] uint32_t Flags() const;
    void Claim(uint16_t connection);
    void Deliver(uint8_t source, const uint8_t* data, uint32_t length);
    void PublishState();
    void TuneLink(uint16_t connection);

    SemaphoreHandle_t callback_mutex_{};
    SemaphoreHandle_t status_mutex_{};
    QueueHandle_t reply_queue_{};
    Text* status_text_{};
    Text* status_value_{};
    Text* scratch_{};
    Text* inbox_{};  // NimBLE host task only
    uint8_t value_[MICROPIXEL_BLE_LINK_MAX_VALUE]{};
    uint16_t value_length_{};
    device::BleLinkCallbacks callbacks_{};
    std::atomic<uint16_t> connection_{0xFFFFU};
    std::atomic<uint32_t> subscribed_{};  // bit per channel
    std::atomic<bool> encrypted_{};
    std::atomic<bool> started_{};
    std::atomic<bool> room_armed_{};
    std::atomic<int32_t> control_pending_{};
    std::atomic<uint32_t> last_message_ms_{};
    std::atomic<uint32_t> bulk_drops_{};
    std::atomic<uint32_t> reply_drops_{};
    std::atomic<uint32_t> bulk_kbps_{};
    uint32_t window_start_ms_{};
    uint32_t window_bytes_{};
    bool status_pending_{};  // guarded by status_mutex_
    bool registered_{};
};

}  // namespace micropixel::platform::bluetooth

#endif
