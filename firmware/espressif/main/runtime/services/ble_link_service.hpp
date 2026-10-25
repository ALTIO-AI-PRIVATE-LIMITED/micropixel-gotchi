#ifndef MICROPIXEL_RUNTIME_SERVICES_BLE_LINK_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_BLE_LINK_SERVICE_HPP

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

// BLE Link 1.0: messages the peer writes queue in PSRAM until the Guest READs
// them; INBOX is advisory and re-armed by READ, like Peer Radio's PACKET.
// STATE and ROOM are advisory too. The link unbinds with the session; the
// GATT service and the connection belong to the Host keyboard.
class BleLinkService final {
   public:
    static constexpr uint32_t kQueueSlots = 8U;

    BleLinkService(device::BleLinkService& link, EventQueue& events, TimerService& clock);
    BleLinkService(const BleLinkService&) = delete;
    BleLinkService& operator=(const BleLinkService&) = delete;
    ~BleLinkService();

    [[nodiscard]] bool valid() const { return mutex_ != nullptr; }  // NOLINT(readability-identifier-naming)
    [[nodiscard]] micropixel_ble_link_state_t State() const { return link_.State(); }
    [[nodiscard]] ServiceResult<void> Start();
    [[nodiscard]] ServiceResult<void> Stop();
    // Copies the oldest message into `data_out` (Guest memory, `capacity`).
    [[nodiscard]] ServiceResult<micropixel_ble_link_read_response_t> Read(uint8_t* data_out, uint32_t capacity);
    [[nodiscard]] ServiceResult<void> Notify(uint8_t channel, const uint8_t* data, uint32_t length);
    [[nodiscard]] ServiceResult<void> SetValue(const uint8_t* data, uint32_t length);
    void Shutdown();

   private:
    struct Slot final {
        uint16_t length{};
        uint8_t source{};
        uint8_t data[MICROPIXEL_BLE_LINK_MAX_MESSAGE]{};
    };

    static void OnMessage(void* context, uint8_t source, const uint8_t* data, uint32_t length);
    static void OnState(void* context, uint32_t flags);
    static void OnRoom(void* context);
    void Post(uint16_t event_id, uint32_t flags, uint32_t pending);

    device::BleLinkService& link_;
    EventQueue& events_;
    TimerService& clock_;
    SemaphoreHandle_t mutex_{};
    Slot* slots_{};
    // Guarded by mutex_.
    uint32_t head_{};
    uint32_t count_{};
    uint32_t dropped_{};
    bool inbox_armed_{};
    bool started_{};
    std::atomic<uint32_t> event_sequence_{0U};
};

}  // namespace micropixel::runtime

#endif
