// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the AirPlay receiver (airplay/*.c, ported from UxPlay) on the Host's Network
// service: owns its sockets, pushes their bytes into the C code, implements its host
// hooks and advertises it over mDNS.
#ifndef IPHONE_AGENT_AIRPLAY_HOST_HPP
#define IPHONE_AGENT_AIRPLAY_HOST_HPP

#include <stdint.h>

#include "airplay/airplay.h"
#include "sdk/micropixel.hpp"

namespace agent {

class AirPlayHost final {
   public:
    explicit AirPlayHost(micropixel::Application& app);

    // Listens on the RTSP port and advertises; false when the port or crypto fails.
    [[nodiscard]] bool Start(const airplay_cfg& config);
    // Re-publishes the mDNS adverts (after the Wi-Fi link changed).
    void Readvertise();
    void OnSocketReady(uint32_t socket);
    void Tick();
    [[nodiscard]] int State() const;
    [[nodiscard]] bool started() const { return rtsp_listener_ != 0U; }

    // Host hooks of airplay/ap_internal.h.
    [[nodiscard]] bool Send(uint32_t socket, const void* data, uint32_t length) const;
    void CloseSocket(uint32_t socket);
    [[nodiscard]] bool OpenMirror();
    void CloseMirror();
    void SendNtp(const uint8_t (&request)[32], uint32_t address, uint16_t port) const;
    [[nodiscard]] uint64_t NowUs() const;
    void Random(uint8_t* out, uint32_t length) const;
    void Log(const char* message) const;

   private:
    static constexpr uint32_t kMaxConnections = 3U;
    static constexpr uint32_t kReadsPerReady = 16U;

    struct Connection final {
        uint32_t socket{};
        void* conn{};  // ap_conn*
    };

    void AcceptRtsp();
    void ReadRtsp(Connection& connection);
    void AcceptMirror();
    void ReadMirror();
    void Drain(uint32_t socket, bool timing);
    void Advertise();
    void Withdraw();

    micropixel::Application& app_;
    micropixel::Network network_;
    uint32_t rtsp_listener_{};
    Connection connections_[kMaxConnections]{};
    uint32_t mirror_listener_{};
    uint32_t mirror_data_{};
    uint32_t timing_{};
    uint32_t audio_[2]{};
    uint32_t adverts_[2]{};
};

}  // namespace agent

#endif
