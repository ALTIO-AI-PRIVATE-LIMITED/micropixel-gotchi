#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "device/contracts/peer_radio.hpp"
#include "device/contracts/wifi.hpp"
#include "esp_event.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace micropixel::platform::wifi {

// ESP-NOW link to one paired device of the same kind, on the STA interface.
//
// Channel policy: while the STA is associated the link sits on the access
// point's channel (the radio cannot leave it). Otherwise it stays wherever
// the peer was last heard, and when the peer goes quiet it searches: the
// rendezvous channel 1 for 600 ms, then one other channel for 300 ms, in
// turn. A paired device beacons every 250 ms, so a searching device finds an
// associated one within one sweep (about 11 s worst case) and two idle ones
// meet on channel 1.
//
// Every frame starts with "CW" and a type byte. HELLO (broadcast, clear) is
// only sent while pairing; BEACON and DATA go unicast to the paired peer with
// ESP-NOW CCMP using a key derived from both addresses. That keeps other
// ESP-NOW devices out but is not secret from someone who knows the addresses.
class EspNowPeerRadio final : public device::PeerRadio {
   public:
    explicit EspNowPeerRadio(device::Wifi& wifi) : wifi_(wifi) {}

    // Creates the radio task; the radio itself starts with the first Start().
    [[nodiscard]] esp_err_t Initialize();

    [[nodiscard]] micropixel_peer_radio_state_t State() const override;
    [[nodiscard]] int32_t Start(const device::PeerRadioCallbacks& callbacks) override;
    [[nodiscard]] int32_t Stop() override;
    [[nodiscard]] int32_t BeginPairing() override;
    [[nodiscard]] int32_t AcceptPeer(const uint8_t (&address)[6]) override;
    [[nodiscard]] int32_t Forget() override;
    [[nodiscard]] int32_t Send(const uint8_t* data, uint32_t length) override;

   private:
    using Address = std::array<uint8_t, 6U>;

    struct RxItem final {
        Address source{};
        int8_t rssi{};
        uint8_t length{};
        uint8_t data[ESP_NOW_MAX_DATA_LEN]{};
    };

    static void TaskEntry(void* argument);
    static void OnReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length);
    static void OnSent(const esp_now_send_info_t* info, esp_now_send_status_t status);
    static void OnWifiEvent(void* context, esp_event_base_t base, int32_t id, void* data);
    void Run();
    void HandleFrame(const RxItem& item, int64_t now_us);
    void Tick(int64_t now_us);
    void UpdateChannel(int64_t now_us);
    void PublishState(bool force);
    [[nodiscard]] esp_err_t EnsureEspNow();
    [[nodiscard]] esp_err_t AddPeer(const Address& address);
    void LoadPeer();
    void SendControl(const uint8_t* destination, uint8_t type);
    [[nodiscard]] uint32_t FlagsLocked(int64_t now_us) const;
    [[nodiscard]] static uint16_t PairCode(const Address& first, const Address& second);

    device::Wifi& wifi_;
    mutable SemaphoreHandle_t mutex_{};
    QueueHandle_t rx_queue_{};
    TaskHandle_t task_{};
    // Guarded by mutex_.
    device::PeerRadioCallbacks callbacks_{};
    Address self_{};
    Address peer_{};
    Address candidate_{};
    bool paired_{};
    bool candidate_reported_{};
    int64_t pairing_until_us_{};
    int64_t peer_heard_us_{};
    int8_t peer_rssi_{};
    uint8_t channel_{1U};
    bool wifi_channel_{};
    uint32_t published_flags_{};
    uint8_t published_channel_{};
    // Radio-task state.
    int64_t next_beacon_us_{};
    int64_t dwell_until_us_{};
    uint8_t sweep_channel_{2U};
    bool on_rendezvous_{true};
    std::atomic<bool> running_{};
    std::atomic<uint32_t> packets_sent_{};
    std::atomic<uint32_t> packets_received_{};
    std::atomic<uint32_t> send_failures_{};
    bool esp_now_ready_{};
    esp_event_handler_instance_t wifi_events_{};
    // Written by the Wi-Fi event task.
    std::atomic<bool> associated_{};
    std::atomic<uint8_t> ap_channel_{};
};

}  // namespace micropixel::platform::wifi
