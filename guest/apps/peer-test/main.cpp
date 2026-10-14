// SPDX-License-Identifier: Apache-2.0
//
// Radio Test: pairs two Gotchis over the Peer Radio (ESP-NOW) and pings
// between them twice a second, showing round-trip time, loss and signal.
// Bring-up check before the walkie-talkie sends audio over the same link.
//
// Controls: BOOT (or tap) starts pairing, and accepts the code once it
// shows. VOL- forgets the pairing.
#include <stdint.h>

#include <cstring>

#include "sdk/micropixel.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);
constexpr Color kBad = Color::Rgb(240U, 72U, 88U);

constexpr uint8_t kPing = 'P';
constexpr uint8_t kPong = 'R';

struct Ping final {
    uint8_t type;
    uint8_t reserved[3];
    uint32_t sequence;
    uint64_t sent_us;
};

class Text final {
   public:
    Text& Add(const char* text) {
        while (*text != '\0' && length_ + 1U < sizeof(buffer_)) {
            buffer_[length_++] = *text++;
        }
        buffer_[length_] = '\0';
        return *this;
    }
    Text& Number(int32_t value) {
        if (value < 0) {
            Add("-");
            value = -value;
        }
        char digits[12];
        uint32_t count = 0U;
        uint32_t magnitude = static_cast<uint32_t>(value);
        do {
            digits[count++] = static_cast<char>('0' + magnitude % 10U);
            magnitude /= 10U;
        } while (magnitude != 0U);
        char reversed[12];
        for (uint32_t index = 0U; index < count; ++index) {
            reversed[index] = digits[count - 1U - index];
        }
        reversed[count] = '\0';
        return Add(reversed);
    }
    Text& Code(uint16_t code) {
        const char digits[5] = {static_cast<char>('0' + code / 1000U % 10U), static_cast<char>('0' + code / 100U % 10U),
                                static_cast<char>('0' + code / 10U % 10U), static_cast<char>('0' + code % 10U), '\0'};
        return Add(digits);
    }
    Text& Address(const uint8_t (&address)[6]) {
        constexpr char kHex[] = "0123456789ABCDEF";
        for (uint32_t index = 0U; index < 6U; ++index) {
            const char pair[4] = {kHex[address[index] >> 4U], kHex[address[index] & 0x0FU],
                                  index < 5U ? ':' : '\0', '\0'};
            Add(pair);
        }
        return *this;
    }
    [[nodiscard]] const char* c_str() const { return buffer_; }

   private:
    char buffer_[64]{};
    uint32_t length_{};
};

class PeerTestApp final {
   public:
    explicit PeerTestApp(micropixel::Application& app)
        : app_(app),
          radio_(app.peer_radio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(500U)).value()) {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        const int32_t margin = width * 5 / 100;
        (void)scene_.CreateLabel({margin, height * 3 / 100}, "Radio Test", kText, SystemFont::kMedium).value();
        status_ = scene_.CreateLabel({margin, height * 16 / 100}, "", kText, SystemFont::kLarge).value();
        line1_ = scene_.CreateLabel({margin, height * 36 / 100}, "", kText, SystemFont::kMedium).value();
        line2_ = scene_.CreateLabel({margin, height * 50 / 100}, "", kMuted, SystemFont::kSmall).value();
        line3_ = scene_.CreateLabel({margin, height * 61 / 100}, "", kMuted, SystemFont::kSmall).value();
        line4_ = scene_.CreateLabel({margin, height * 72 / 100}, "", kMuted, SystemFont::kSmall).value();
        hint_ = scene_.CreateLabel({margin, height * 87 / 100}, "", kMuted, SystemFont::kSmall).value();
        if (const auto started = radio_.Start(); !started) {
            status_.SetText("Radio unavailable");
            status_.SetColor(kBad);
        }
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (const auto* peer = event.peer_radio()) {
            if (peer->kind() == micropixel::PeerRadioEventKind::kPacket) {
                Drain();
            } else if (peer->kind() == micropixel::PeerRadioEventKind::kCandidate) {
                std::memcpy(candidate_, peer->address(), sizeof(candidate_));
                candidate_code_ = peer->code();
                has_candidate_ = true;
                app_.log().Info(Text{}.Add("candidate code ").Code(peer->code()).c_str());
            }
            Refresh();
        } else if (event.TimerFrom(tick_) != nullptr) {
            SendPing();
            Refresh();
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown) {
                if (key->code() == micropixel::KeyCode::kConfirm) {
                    Primary();
                } else if (key->code() == micropixel::KeyCode::kLeft) {
                    (void)radio_.Forget();
                    has_candidate_ = false;
                    Refresh();
                }
            }
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kUp) {
                Primary();
            }
        } else if (event.type() == micropixel::EventType::kStop) {
            (void)radio_.Stop();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    void Primary() {
        if (has_candidate_) {
            if (radio_.Accept(candidate_)) {
                has_candidate_ = false;
                sent_ = 0U;
                answered_ = 0U;
            }
        } else {
            (void)radio_.BeginPairing();
        }
        Refresh();
    }

    void SendPing() {
        const auto state = radio_.State();
        if (!state || !state->paired) {
            return;
        }
        Ping ping{};
        ping.type = kPing;
        ping.sequence = ++sequence_;
        ping.sent_us = app_.clock().Now().microseconds();
        if (radio_.Send(&ping, sizeof(ping))) {
            ++sent_;
        }
    }

    void Drain() {
        uint8_t data[micropixel::PeerRadio::kMaxPayload];
        for (;;) {
            const auto packet = radio_.Receive(data);
            if (!packet || packet->length == 0U) {
                return;
            }
            last_rssi_ = packet->rssi;
            if (packet->length != sizeof(Ping)) {
                continue;
            }
            Ping ping{};
            std::memcpy(&ping, data, sizeof(ping));
            if (ping.type == kPing) {
                ping.type = kPong;
                (void)radio_.Send(&ping, sizeof(ping));
            } else if (ping.type == kPong) {
                ++answered_;
                rtt_ms_ = static_cast<int32_t>((app_.clock().Now().microseconds() - ping.sent_us) / 1000U);
            }
        }
    }

    void Refresh() {
        const auto state = radio_.State();
        if (!state || !state->started) {
            Present();
            return;
        }
        if (has_candidate_) {
            status_.SetText(Text{}.Add("Code ").Code(candidate_code_).c_str());
            status_.SetColor(kWarn);
            line1_.SetText("Same code on both? Press BOOT");
        } else if (state->pairing) {
            status_.SetText("Pairing...");
            status_.SetColor(kWarn);
            line1_.SetText("Press BOOT on the other one too");
        } else if (!state->paired) {
            status_.SetText("Not paired");
            status_.SetColor(kMuted);
            line1_.SetText("Press BOOT on both to pair");
        } else if (state->peer_present) {
            status_.SetText("Linked");
            status_.SetColor(kGood);
            const uint32_t lost = sent_ > answered_ ? sent_ - answered_ : 0U;
            line1_.SetText(Text{}
                               .Add("RTT ")
                               .Number(rtt_ms_)
                               .Add(" ms  RSSI ")
                               .Number(last_rssi_)
                               .Add("  lost ")
                               .Number(static_cast<int32_t>(lost))
                               .Add("/")
                               .Number(static_cast<int32_t>(sent_))
                               .c_str());
        } else {
            status_.SetText("Searching...");
            status_.SetColor(kWarn);
            line1_.SetText("Peer not heard yet");
        }
        line2_.SetText(Text{}.Add("Me   ").Address(state->self_address).c_str());
        line3_.SetText(state->paired ? Text{}.Add("Peer ").Address(state->peer_address).c_str() : "Peer -");
        line4_.SetText(Text{}
                           .Add("Ch ")
                           .Number(state->channel)
                           .Add(state->on_wifi_channel ? " (Wi-Fi)" : "")
                           .Add("  tx ")
                           .Number(static_cast<int32_t>(state->packets_sent))
                           .Add(" rx ")
                           .Number(static_cast<int32_t>(state->packets_received))
                           .Add(" fail ")
                           .Number(static_cast<int32_t>(state->send_failures))
                           .c_str());
        hint_.SetText(state->paired ? "BOOT: re-pair   VOL-: forget" : "BOOT or tap: pair");
        Present();
    }

    void Present() { (void)app_.renderer().Present(scene_); }

    micropixel::Application& app_;
    micropixel::PeerRadio radio_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::LabelNode status_;
    micropixel::LabelNode line1_;
    micropixel::LabelNode line2_;
    micropixel::LabelNode line3_;
    micropixel::LabelNode line4_;
    micropixel::LabelNode hint_;
    uint8_t candidate_[6]{};
    uint16_t candidate_code_{};
    bool has_candidate_{};
    uint32_t sequence_{};
    uint32_t sent_{};
    uint32_t answered_{};
    int32_t rtt_ms_{};
    int32_t last_rssi_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    PeerTestApp peer_test(app);
    app.log().Info("Radio Test ready");
    app.Run([&peer_test](const micropixel::Event& event) { return peer_test.Handle(event); });
    return 0;
}
