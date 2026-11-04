// SPDX-License-Identifier: Apache-2.0
//
// DevCon Wall: nine Gotchis in a 3x3 grid play one Devcon 8 India animation
// together. Each Gotchi shows its own 296x240 tile of an 888x720 canvas.
//
// Setup: on first start each Gotchi asks for its spot (1-9, row by row from
// the top left, as seen from the front). Spot 5, the centre, leads: it
// broadcasts its show clock over the Wi-Fi network every Gotchi has joined
// (UDP, port 47805), and the others follow it. No frames go over the air;
// every Gotchi draws the same scene for the same time and keeps its tile.
//
// Controls while playing: tap shows the spot map and the sync dot (green
// synced, amber looking for the leader, red no Wi-Fi). Hold 1.5 s, or press
// BOOT, to pick the spot again.
#include <stdint.h>

#include <cstring>

#include "sdk/micropixel.hpp"
#include "wall_render.hpp"

namespace {

namespace mp = micropixel;
using devcon_wall::LinkStatus;

constexpr char kSpotKey[] = "spot";
constexpr uint16_t kPort = 47805U;
constexpr uint32_t kBroadcast = 0xFFFFFFFFU;
constexpr uint64_t kBeatIntervalUs = 200000U;
constexpr uint64_t kLeaderTimeoutUs = 2500000U;
constexpr uint64_t kBadgeUs = 6000000U;
constexpr uint64_t kHoldUs = 1500000U;
constexpr uint64_t kStateIntervalUs = 1000000U;
constexpr uint32_t kOffsetSamples = 20U;  // 4 s of beats.

constexpr mp::Color kBackground = mp::Color::Rgb(22U, 18U, 60U);
constexpr mp::Color kText = mp::Color::Rgb(250U, 238U, 255U);
constexpr mp::Color kMuted = mp::Color::Rgb(170U, 160U, 210U);
constexpr mp::Color kCell = mp::Color::Rgb(70U, 56U, 150U);
constexpr mp::Color kCellStroke = mp::Color::Rgb(120U, 232U, 205U);
constexpr mp::Color kChosen = mp::Color::Rgb(245U, 160U, 60U);

struct Beat final {
    char magic[4];
    uint32_t epoch;
    uint64_t leader_us;
};
constexpr char kMagic[4] = {'D', 'C', 'W', '1'};

enum class Next : uint8_t { kPlay, kPick, kStop };

class WallApp final {
   public:
    explicit WallApp(mp::Application& app) : app_(app), network_(app.network()) {}

    int Run() {
        (void)app_.renderer().ConfigureDisplay({});
        OpenNetwork();
        spot_ = app_.storage().GetU32Or(kSpotKey, 0U);
        Next next = spot_ >= 1U && spot_ <= 9U ? Next::kPlay : Next::kPick;
        while (next != Next::kStop) {
            next = next == Next::kPick ? Pick() : Play();
        }
        return 0;
    }

   private:
    // ---- clock sync ---------------------------------------------------------
    [[nodiscard]] uint64_t Now() const { return app_.clock().Now().microseconds(); }
    [[nodiscard]] bool Leader() const { return spot_ == devcon_wall::kLeaderSpot; }

    void OpenNetwork() {
        if (auto socket = network_.OpenUdp(kPort)) {
            udp_ = *socket;
            have_udp_ = true;
        } else {
            app_.log().Error("devcon-wall: UDP port unavailable");
        }
        epoch_ = static_cast<uint32_t>(Now() ^ 0x9E3779B9U) | 1U;
    }

    void Drain() {
        if (!have_udp_) return;
        for (;;) {
            Beat beat{};
            auto received = network_.Receive(udp_, &beat, sizeof(beat));
            if (!received || received->length == 0U) return;
            if (received->length != sizeof(beat) || std::memcmp(beat.magic, kMagic, sizeof(kMagic)) != 0 ||
                Leader()) {
                continue;
            }
            const uint64_t now = Now();
            if (beat.epoch != leader_epoch_) {
                leader_epoch_ = beat.epoch;
                sample_count_ = 0U;
            }
            // Delivery only ever adds delay, so the largest offset in the
            // window is the closest to the leader's real clock.
            samples_[sample_next_] = static_cast<int64_t>(beat.leader_us) - static_cast<int64_t>(now);
            sample_next_ = (sample_next_ + 1U) % kOffsetSamples;
            if (sample_count_ < kOffsetSamples) ++sample_count_;
            int64_t best = samples_[(sample_next_ + kOffsetSamples - 1U) % kOffsetSamples];
            for (uint32_t i = 0; i < sample_count_; ++i) {
                const int64_t s = samples_[(sample_next_ + kOffsetSamples - 1U - i) % kOffsetSamples];
                if (s > best) best = s;
            }
            offset_us_ = best;
            heard_us_ = now;
        }
    }

    void Tick(uint64_t now) {
        if (now >= next_state_us_) {
            next_state_us_ = now + kStateIntervalUs;
            const auto state = network_.State();
            connected_ = state && state->station_connected;
        }
        if (Leader() && have_udp_ && connected_ && now >= next_beat_us_) {
            next_beat_us_ = now + kBeatIntervalUs;
            Beat beat{};
            std::memcpy(beat.magic, kMagic, sizeof(kMagic));
            beat.epoch = epoch_;
            beat.leader_us = now;
            (void)network_.SendTo(udp_, &beat, sizeof(beat), kBroadcast, kPort);
        }
    }

    [[nodiscard]] LinkStatus Status(uint64_t now) const {
        if (!connected_) return LinkStatus::kOffline;
        if (Leader()) return LinkStatus::kSynced;
        return heard_us_ != 0U && now - heard_us_ < kLeaderTimeoutUs ? LinkStatus::kSynced : LinkStatus::kSearching;
    }

    [[nodiscard]] uint32_t ShowMs(uint64_t now) const {
        const int64_t show_us = static_cast<int64_t>(now) + (Leader() ? 0 : offset_us_);
        return static_cast<uint32_t>(static_cast<uint64_t>(show_us < 0 ? 0 : show_us) / 1000U %
                                     devcon_wall::kLoopMs);
    }

    // ---- spot picker (Scene UI) ---------------------------------------------
    Next Pick() {
        auto created = app_.renderer().CreateScene(kBackground);
        if (!created) return Next::kStop;
        mp::Scene scene = std::move(*created);
        const auto info = app_.renderer().info();
        const int32_t width = static_cast<int32_t>(info.width());
        constexpr int32_t kCellW = 56;
        constexpr int32_t kCellH = 42;
        constexpr int32_t kGap = 6;
        const int32_t grid_x = (width - (3 * kCellW + 2 * kGap)) / 2;
        constexpr int32_t kGridY = 44;
        (void)scene.CreateLabel({width / 2, 18}, "Which spot is this Gotchi?", kText, mp::SystemFont::kMedium, true);
        mp::RoundedRectNode cells[9];
        mp::Rect rects[9];
        static const char* const kNames[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
        for (int32_t i = 0; i < 9; ++i) {
            rects[i] = {grid_x + (i % 3) * (kCellW + kGap), kGridY + (i / 3) * (kCellH + kGap), kCellW, kCellH};
            mp::RoundedRectStyle style{};
            style.fill = kCell;
            style.stroke = kCellStroke;
            style.radius = 8U;
            style.stroke_width = i == 4 ? 2U : 0U;
            if (auto node = scene.CreateRoundedRect(rects[i], style)) cells[i] = *node;
            (void)scene.CreateLabel({rects[i].x + kCellW / 2, rects[i].y + kCellH / 2 - 9}, kNames[i], kText,
                                    mp::SystemFont::kLarge, true);
        }
        (void)scene.CreateLabel({width / 2, 196}, "Facing the wall, 1 is top left.", kMuted, mp::SystemFont::kSmall,
                                true);
        (void)scene.CreateLabel({width / 2, 214}, "5 (centre) leads the sync. Tap to choose.", kMuted,
                                mp::SystemFont::kSmall, true);
        int32_t selected = spot_ >= 1U && spot_ <= 9U ? static_cast<int32_t>(spot_) - 1 : 4;
        auto paint = [&]() {
            for (int32_t i = 0; i < 9; ++i) cells[i].SetFillColor(i == selected ? kChosen : kCell);
            (void)app_.renderer().Present(scene);
        };
        paint();
        for (;;) {
            mp::Event event;
            if (app_.WaitEventFor(event, mp::Duration::Milliseconds(200U))) {
                if (event.type() == mp::EventType::kStop) return Next::kStop;
                if (const auto* network = event.network();
                    network != nullptr && network->kind() == mp::NetworkEventKind::kReady) {
                    Drain();
                } else if (const auto* touch = event.touch()) {
                    for (int32_t i = 0; i < 9; ++i) {
                        const mp::Rect& r = rects[i];
                        if (touch->x() >= r.x && touch->x() < r.x + r.width && touch->y() >= r.y &&
                            touch->y() < r.y + r.height) {
                            selected = i;
                            paint();
                            if (touch->phase() == mp::TouchPhase::kUp) return Choose(selected);
                        }
                    }
                } else if (const auto* key = event.key(); key != nullptr && key->phase() == mp::KeyPhase::kDown) {
                    switch (key->code()) {
                        case mp::KeyCode::kLeft: selected = (selected + 8) % 9; break;
                        case mp::KeyCode::kRight: selected = (selected + 1) % 9; break;
                        case mp::KeyCode::kUp: selected = (selected + 6) % 9; break;
                        case mp::KeyCode::kDown: selected = (selected + 3) % 9; break;
                        case mp::KeyCode::kConfirm: return Choose(selected);
                        default: break;
                    }
                    paint();
                }
            }
            Tick(Now());
        }
    }

    Next Choose(int32_t index) {
        spot_ = static_cast<uint32_t>(index) + 1U;
        (void)app_.storage().SetU32(kSpotKey, spot_);
        sample_count_ = 0U;
        heard_us_ = 0U;
        app_.log().Info(Leader() ? "devcon-wall: spot 5, leading" : "devcon-wall: spot chosen, following");
        return Next::kPlay;
    }

    // ---- wall playback (GuestSurface) ---------------------------------------
    Next Play() {
        auto created = app_.renderer().CreateGuestSurface(2U, 1U);
        if (!created) {
            app_.log().Error("devcon-wall: no GuestSurface (pinned_memory?)");
            return Next::kStop;
        }
        mp::GuestSurface surface = std::move(*created);
        const int32_t pitch = static_cast<int32_t>(surface.buffer_width());
        const int32_t height = static_cast<int32_t>(surface.buffer_height());
        const bool swap = surface.rgb565_byte_swapped();
        const devcon_wall::Tile tile = devcon_wall::TileForSpot(spot_);
        for (uint32_t b = 0; b < surface.buffer_count(); ++b) {
            if (uint16_t* pixels = surface.Buffer(b)) std::memset(pixels, 0, surface.buffer_bytes());
        }
        badge_until_us_ = Now() + kBadgeUs;
        touching_ = false;
        frames_ = 0U;
        stats_since_us_ = Now();
        Next next = Next::kPlay;
        while (next == Next::kPlay) {
            mp::Event event;
            while (next == Next::kPlay && app_.PollEvent(event)) next = Handle(event);
            uint32_t index = 0U;
            while (next == Next::kPlay && !surface.AcquireFree(index)) {
                if (app_.WaitEventFor(event, mp::Duration::Milliseconds(50U))) next = Handle(event);
                Tick(Now());
            }
            if (next != Next::kPlay) break;
            const uint64_t now = Now();
            Tick(now);
            if (touching_ && now - touch_down_us_ >= kHoldUs) {
                next = Next::kPick;
                break;
            }
            uint16_t* pixels = surface.Buffer(index);
            if (pixels == nullptr) break;
            if (pitch >= devcon_wall::kTileWidth && height >= devcon_wall::kTileHeight) {
                devcon_wall::RenderTile(tile, ShowMs(now), pixels, pitch);
                if (now < badge_until_us_) devcon_wall::RenderBadge(spot_, Status(now), pixels, pitch);
            }
            if (swap) {
                const uint32_t count = surface.buffer_bytes() / 2U;
                for (uint32_t i = 0; i < count; ++i) {
                    pixels[i] = static_cast<uint16_t>((pixels[i] << 8U) | (pixels[i] >> 8U));
                }
            }
            ++frames_;
            if (now - stats_since_us_ >= 10000000U) {
                LogFps(now);
            }
            if (!surface.Present(index)) {
                app_.log().Error("devcon-wall: present rejected");
                next = Next::kStop;
            }
        }
        surface.Reset();
        return next;
    }

    void LogFps(uint64_t now) {
        // "fps x10" and the sync state, every 10 s, for tuning on hardware.
        char line[64] = "devcon-wall: fps10=";
        uint32_t length = 19U;
        uint32_t value = static_cast<uint32_t>(static_cast<uint64_t>(frames_) * 10000000U / (now - stats_since_us_));
        char digits[10];
        uint32_t count = 0U;
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0U && count < sizeof(digits));
        while (count > 0U) line[length++] = digits[--count];
        const char* status = Status(now) == LinkStatus::kSynced
                                 ? " synced"
                                 : (Status(now) == LinkStatus::kSearching ? " searching" : " offline");
        for (const char* c = status; *c != '\0' && length + 1U < sizeof(line); ++c) line[length++] = *c;
        line[length] = '\0';
        app_.log().Info(line);
        frames_ = 0U;
        stats_since_us_ = now;
    }

    Next Handle(const mp::Event& event) {
        if (event.type() == mp::EventType::kStop) return Next::kStop;
        if (const auto* network = event.network()) {
            if (network->kind() == mp::NetworkEventKind::kReady) Drain();
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == mp::TouchPhase::kDown) {
                touching_ = true;
                touch_down_us_ = Now();
            } else if (touch->phase() == mp::TouchPhase::kUp || touch->phase() == mp::TouchPhase::kCancel) {
                if (touching_) badge_until_us_ = Now() + kBadgeUs;
                touching_ = false;
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == mp::KeyPhase::kDown && key->code() == mp::KeyCode::kConfirm) return Next::kPick;
        }
        return Next::kPlay;
    }

    mp::Application& app_;
    mp::Network network_;
    uint32_t spot_{};
    uint32_t udp_{};
    bool have_udp_{};
    bool connected_{};
    uint32_t epoch_{};
    uint32_t leader_epoch_{};
    int64_t samples_[kOffsetSamples]{};
    uint32_t sample_next_{};
    uint32_t sample_count_{};
    int64_t offset_us_{};
    uint64_t heard_us_{};
    uint64_t next_beat_us_{};
    uint64_t next_state_us_{};
    uint64_t badge_until_us_{};
    uint64_t touch_down_us_{};
    bool touching_{};
    uint32_t frames_{};
    uint64_t stats_since_us_{};
};

}  // namespace

int main() {
    mp::Application app;
    WallApp wall(app);
    app.log().Info("DevCon Wall ready");
    return wall.Run();
}
