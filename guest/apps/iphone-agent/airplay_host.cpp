// SPDX-License-Identifier: GPL-3.0-or-later
#include "airplay_host.hpp"

#include "airplay/ap_internal.h"

namespace agent {
namespace {

using micropixel::ErrorCode;

// The C code reaches the App through free functions; one receiver per App.
AirPlayHost* active = nullptr;

uint8_t read_buffer[micropixel::Network::kMaxTransfer];

bool WouldBlock(const micropixel::Error& error) { return error.code() == ErrorCode::kWouldBlock; }

}  // namespace

AirPlayHost::AirPlayHost(micropixel::Application& app) : app_(app), network_(app.network()) { active = this; }

bool AirPlayHost::Start(const airplay_cfg& config) {
    if (rtsp_listener_ != 0U) {
        return true;
    }
    if (!airplay_start(&config)) {
        Log("airplay: crypto init failed");
        return false;
    }
    auto listener = network_.Listen(AP_RTSP_PORT);
    if (!listener) {
        Log("airplay: cannot listen on the RTSP port");
        return false;
    }
    rtsp_listener_ = *listener;
    Advertise();
    return true;
}

void AirPlayHost::Readvertise() {
    if (rtsp_listener_ == 0U) {
        return;
    }
    Withdraw();
    Advertise();
}

void AirPlayHost::Advertise() {
    uint8_t txt[micropixel::Network::kMaxTxt];
    const uint32_t airplay_length = static_cast<uint32_t>(ap_txt_record(true, txt, sizeof(txt)));
    if (auto advert = network_.Advertise("_airplay", ap_cfg.name, AP_RTSP_PORT, txt, airplay_length)) {
        adverts_[0] = *advert;
    } else {
        Log("airplay: _airplay advert failed");
    }
    char raop[64];
    ap_raop_instance(raop, sizeof(raop));
    const uint32_t raop_length = static_cast<uint32_t>(ap_txt_record(false, txt, sizeof(txt)));
    if (auto advert = network_.Advertise("_raop", raop, AP_RTSP_PORT, txt, raop_length)) {
        adverts_[1] = *advert;
    } else {
        Log("airplay: _raop advert failed");
    }
}

void AirPlayHost::Withdraw() {
    for (uint32_t& advert : adverts_) {
        if (advert != 0U) {
            (void)network_.Withdraw(advert);
            advert = 0U;
        }
    }
}

void AirPlayHost::OnSocketReady(uint32_t socket) {
    if (socket == 0U) {
        return;
    }
    if (socket == rtsp_listener_) {
        AcceptRtsp();
        return;
    }
    for (Connection& connection : connections_) {
        if (connection.socket == socket) {
            ReadRtsp(connection);
            return;
        }
    }
    if (socket == mirror_listener_) {
        AcceptMirror();
    } else if (socket == mirror_data_) {
        ReadMirror();
    } else if (socket == timing_) {
        Drain(socket, true);
    } else if (socket == audio_[0] || socket == audio_[1]) {
        Drain(socket, false);
    }
}

void AirPlayHost::AcceptRtsp() {
    for (;;) {
        auto accepted = network_.Accept(rtsp_listener_);
        if (!accepted) {
            return;
        }
        Connection* slot = nullptr;
        for (Connection& connection : connections_) {
            if (connection.socket == 0U) {
                slot = &connection;
                break;
            }
        }
        ap_conn* conn = slot != nullptr ? ap_conn_open(accepted->socket, accepted->remote_address) : nullptr;
        if (conn == nullptr) {
            (void)network_.Close(accepted->socket);
            continue;
        }
        slot->socket = accepted->socket;
        slot->conn = conn;
    }
}

void AirPlayHost::ReadRtsp(Connection& connection) {
    for (uint32_t round = 0U; round < kReadsPerReady && connection.socket != 0U; ++round) {
        auto received = network_.Receive(connection.socket, read_buffer, sizeof(read_buffer));
        if (!received) {
            if (WouldBlock(received.error())) {
                return;
            }
        } else if (!received->closed && received->length != 0U) {
            if (ap_conn_data(static_cast<ap_conn*>(connection.conn), read_buffer, received->length)) {
                continue;
            }
        }
        // Peer closed, an error, or the request could not be served.
        auto* conn = static_cast<ap_conn*>(connection.conn);
        connection = {};
        ap_conn_close(conn);  // closes the socket through ap_socket_close()
        return;
    }
}

bool AirPlayHost::OpenMirror() {
    CloseMirror();
    auto listener = network_.Listen(AP_MIRROR_PORT);
    if (!listener) {
        Log("mirror: cannot listen on the data port");
        return false;
    }
    mirror_listener_ = *listener;
    if (auto timing = network_.OpenUdp(AP_TIMING_PORT)) {
        timing_ = *timing;
    }
    if (auto audio = network_.OpenUdp(AP_AUDIO_DATA_PORT)) {
        audio_[0] = *audio;
    }
    if (auto audio = network_.OpenUdp(AP_AUDIO_CTRL_PORT)) {
        audio_[1] = *audio;
    }
    return true;
}

void AirPlayHost::CloseMirror() {
    uint32_t* const sockets[] = {&mirror_data_, &mirror_listener_, &timing_, &audio_[0], &audio_[1]};
    for (uint32_t* socket : sockets) {
        if (*socket != 0U) {
            (void)network_.Close(*socket);
            *socket = 0U;
        }
    }
}

void AirPlayHost::AcceptMirror() {
    if (mirror_data_ != 0U) {
        return;  // one data connection at a time; the next waits in the backlog
    }
    auto accepted = network_.Accept(mirror_listener_);
    if (!accepted) {
        return;
    }
    mirror_data_ = accepted->socket;
    Log("mirror: data connection accepted");
}

void AirPlayHost::ReadMirror() {
    for (uint32_t round = 0U; round < kReadsPerReady && mirror_data_ != 0U; ++round) {
        auto received = network_.Receive(mirror_data_, read_buffer, sizeof(read_buffer));
        if (!received) {
            if (WouldBlock(received.error())) {
                return;
            }
        } else if (!received->closed && received->length != 0U) {
            if (ap_mirror_feed(read_buffer, received->length)) {
                continue;
            }
        }
        (void)network_.Close(mirror_data_);
        mirror_data_ = 0U;
        ap_mirror_data_closed();
        AcceptMirror();  // a waiting connection, if any
        return;
    }
}

void AirPlayHost::Drain(uint32_t socket, bool timing) {
    for (uint32_t round = 0U; round < kReadsPerReady; ++round) {
        auto received = network_.Receive(socket, read_buffer, sizeof(read_buffer));
        if (!received || received->length == 0U) {
            return;
        }
        if (timing) {
            ap_mirror_ntp_reply(read_buffer, received->length);
        }
    }
}

void AirPlayHost::Tick() { ap_mirror_tick(); }

int AirPlayHost::State() const { return rtsp_listener_ != 0U ? airplay_state() : AIRPLAY_IDLE; }

bool AirPlayHost::Send(uint32_t socket, const void* data, uint32_t length) const {
    const auto* bytes = static_cast<const uint8_t*>(data);
    const uint64_t deadline = NowUs() + 500000U;
    while (length != 0U) {
        auto sent = network_.Send(socket, bytes, length);
        if (sent) {
            bytes += *sent;
            length -= *sent;
        } else if (!WouldBlock(sent.error()) || NowUs() > deadline) {
            return false;
        }
    }
    return true;
}

void AirPlayHost::CloseSocket(uint32_t socket) {
    for (Connection& connection : connections_) {
        if (connection.socket == socket) {
            connection = {};
        }
    }
    (void)network_.Close(socket);
}

void AirPlayHost::SendNtp(const uint8_t (&request)[32], uint32_t address, uint16_t port) const {
    if (timing_ != 0U) {
        (void)network_.SendTo(timing_, request, sizeof(request), address, port);
    }
}

uint64_t AirPlayHost::NowUs() const { return app_.clock().Now().microseconds(); }

void AirPlayHost::Random(uint8_t* out, uint32_t length) const {
    for (uint32_t index = 0U; index < length; index += 4U) {
        const uint32_t value = app_.random().U32();
        for (uint32_t byte = 0U; byte < 4U && index + byte < length; ++byte) {
            out[index + byte] = static_cast<uint8_t>(value >> (8U * byte));
        }
    }
}

void AirPlayHost::Log(const char* message) const { app_.log().Info(message); }

}  // namespace agent

extern "C" {

uint64_t ap_now_us(void) { return agent::active->NowUs(); }

void ap_random(uint8_t* out, size_t n) { agent::active->Random(out, static_cast<uint32_t>(n)); }

bool ap_send(uint32_t sock, const void* p, size_t n) {
    return agent::active->Send(sock, p, static_cast<uint32_t>(n));
}

void ap_socket_close(uint32_t sock) { agent::active->CloseSocket(sock); }

bool ap_host_mirror_open(void) { return agent::active->OpenMirror(); }

void ap_host_mirror_close(void) { agent::active->CloseMirror(); }

void ap_host_send_ntp(const uint8_t req[32], uint32_t ip, uint16_t port) {
    agent::active->SendNtp(*reinterpret_cast<const uint8_t(*)[32]>(req), ip, port);
}

}  // extern "C"
