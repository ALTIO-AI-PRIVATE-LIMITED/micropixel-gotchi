#ifndef MICROPIXEL_DEVICE_WIFI_HPP
#define MICROPIXEL_DEVICE_WIFI_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <string_view>

namespace micropixel::device {

constexpr uint32_t kWifiSsidCapacity = 32U;
constexpr uint32_t kWifiPasswordCapacity = 64U;
constexpr uint32_t kMaxSavedWifiNetworks = 8U;
constexpr uint32_t kMaxVisibleWifiNetworks = 16U;

enum class WifiBand : uint8_t {
    kUnknown,
    k2_4Ghz,
    k5Ghz,
};

enum class WifiSecurity : uint8_t {
    kOpen,
    kSecured,
};

enum class WifiConnectionState : uint8_t {
    kDisconnected,
    kConnecting,
    kConnected,
    kAuthenticationFailed,
    kAuthenticationTimedOut,
    kHandshakeTimedOut,
    kNetworkNotFound,
    kFailed,
};

struct WifiNetwork final {
    std::array<char, kWifiSsidCapacity + 1U> ssid{};
    int8_t rssi{};
    uint8_t channel{};
    WifiBand band{WifiBand::kUnknown};
    WifiSecurity security{WifiSecurity::kSecured};
    bool saved{};
    bool connected{};
};

struct WifiSnapshot final {
    std::array<WifiNetwork, kMaxSavedWifiNetworks> saved_networks{};
    std::array<WifiNetwork, kMaxVisibleWifiNetworks> available_networks{};
    uint32_t saved_network_count{};
    uint32_t available_network_count{};
    bool available{};
    bool enabled{};
    bool connected{};
    bool scanning{};
    bool control_pending{};
    bool control_failed{};
    WifiConnectionState connection_state{WifiConnectionState::kDisconnected};
};

enum class WifiError : uint8_t {
    kUnavailable,
    kInvalidArgument,
    kBusy,
    kStorage,
    kOperationFailed,
};

// Addresses of the live links, IPv4 in network byte order (0 when down).
struct WifiLinkInfo final {
    std::array<char, kWifiSsidCapacity + 1U> station_ssid{};
    std::array<uint8_t, 6U> mac{};
    uint32_t station_address{};
    uint32_t access_point_address{};
    uint8_t access_point_stations{};
    uint8_t channel{};
    int8_t rssi{};
    bool station_connected{};
    bool station_connecting{};
    bool access_point_active{};
};

// Invoked from the Wi-Fi event context after its snapshot changes. The sink
// must be non-blocking and must not call back into Wifi.
using WifiStateChangeSink = void (*)(void* context);

class Wifi {
   public:
    virtual ~Wifi() = default;
    Wifi(const Wifi&) = delete;
    Wifi& operator=(const Wifi&) = delete;

    // Starts the platform Wi-Fi lifecycle. A successful return may represent
    // accepted background radio startup; Snapshot().available is the ready
    // signal for operations and state-change sinks report the transition.
    [[nodiscard]] virtual std::expected<void, WifiError> Initialize() = 0;
    [[nodiscard]] virtual WifiSnapshot Snapshot() const = 0;
    virtual void Poll() {}  // Non-blocking request for background status maintenance.
    virtual void SetStateChangeSink(WifiStateChangeSink sink, void* context) = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> SetEnabled(bool enabled) = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> RequestScan() = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> ConnectSaved(std::string_view ssid) = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> Connect(std::string_view ssid, std::string_view password) = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> Disconnect() = 0;
    [[nodiscard]] virtual std::expected<void, WifiError> Forget(std::string_view ssid) = 0;

    // Guest hotspot (Network service). While it runs the station link keeps an
    // existing association but makes no new connection attempts or scans,
    // which would move the radio off the hotspot's channel.
    [[nodiscard]] virtual std::expected<void, WifiError> StartAccessPoint(std::string_view ssid,
                                                                          std::string_view password, uint8_t channel,
                                                                          uint8_t max_stations) {
        (void)ssid;
        (void)password;
        (void)channel;
        (void)max_stations;
        return std::unexpected(WifiError::kUnavailable);
    }
    [[nodiscard]] virtual std::expected<void, WifiError> StopAccessPoint() {
        return std::unexpected(WifiError::kUnavailable);
    }
    [[nodiscard]] virtual WifiLinkInfo LinkInfo() const { return {}; }

   protected:
    Wifi() = default;
};

}  // namespace micropixel::device

#endif
