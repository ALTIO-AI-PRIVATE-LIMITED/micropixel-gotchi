#ifndef MICROPIXEL_SDK_NETWORK_HPP
#define MICROPIXEL_SDK_NETWORK_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

// IPv4 addresses are in network byte order, ports in host byte order.
struct NetworkState final {
    bool available{};
    bool station_connected{};
    bool station_connecting{};
    bool access_point{};
    uint8_t channel{};
    int8_t rssi{};
    uint8_t access_point_stations{};
    uint32_t station_address{};
    uint32_t access_point_address{};
    uint8_t mac[6]{};
    char station_ssid[33]{};
};

struct NetworkAccepted final {
    uint32_t socket{};
    uint32_t remote_address{};
    uint16_t remote_port{};
    uint32_t local_address{};
};

struct NetworkReceived final {
    uint32_t length{};
    // The TCP peer closed (or reset) the connection; Close() the socket.
    bool closed{};
    // UDP: the sender.
    uint32_t remote_address{};
    uint16_t remote_port{};
};

// Wi-Fi and IPv4 sockets for this App. The Host joins saved Wi-Fi by itself;
// StartAccessPoint() turns on a WPA2 hotspot of the App's own instead. Sockets
// never block: Accept(), Receive() and Send() return kWouldBlock when nothing
// is ready, and a Network ready event names the socket once it turns readable
// after the App last called Accept() or Receive() on it. Sockets, adverts and
// the hotspot end with the session.
class Network final {
   public:
    static constexpr uint32_t kMaxTransfer = MICROPIXEL_NETWORK_MAX_TRANSFER;
    static constexpr uint32_t kMaxTxt = MICROPIXEL_NETWORK_MAX_TXT;

    [[nodiscard]] Result<NetworkState> State() const;
    // `password` 8..63 characters; `channel` 0 picks one.
    [[nodiscard]] Result<void> StartAccessPoint(const char* ssid, const char* password, uint8_t channel = 0U,
                                                uint8_t max_stations = 4U) const;
    [[nodiscard]] Result<void> StopAccessPoint() const;
    // Publishes a DNS-SD service such as "_airplay"._tcp. `txt` is a TXT
    // record: length-prefixed "key=value" strings. Returns the advert handle.
    [[nodiscard]] Result<uint32_t> Advertise(const char* service, const char* instance, uint16_t port,
                                             const uint8_t* txt, uint32_t txt_length, bool udp = false) const;
    [[nodiscard]] Result<void> Withdraw(uint32_t advert) const;
    [[nodiscard]] Result<uint32_t> Listen(uint16_t port) const;
    [[nodiscard]] Result<uint32_t> OpenUdp(uint16_t port) const;
    [[nodiscard]] Result<NetworkAccepted> Accept(uint32_t listener) const;
    [[nodiscard]] Result<NetworkReceived> Receive(uint32_t socket, void* data, uint32_t capacity) const;
    // TCP; may send less than `length`.
    [[nodiscard]] Result<uint32_t> Send(uint32_t socket, const void* data, uint32_t length) const;
    [[nodiscard]] Result<uint32_t> SendTo(uint32_t socket, const void* data, uint32_t length, uint32_t address,
                                          uint16_t port) const;
    [[nodiscard]] Result<void> Close(uint32_t socket) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr Network(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
