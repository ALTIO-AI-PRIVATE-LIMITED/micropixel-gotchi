// SPDX-License-Identifier: Apache-2.0
//
// Chat Remote: the Gotchi version of the M5Stack "M5 Chat Remote". Tapping
// WhatsApp or WeChat brings that app to the front on the paired Mac through
// Spotlight (Command-Space, type the name, Return); the volume tiles and the
// VOL-/VOL+ keys send media volume keys. It shares the Host Bluetooth
// keyboard with WisprKey, so one pairing serves both Apps.
#include <stdint.h>

#include <array>
#include <span>

#include "chat-remote_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::HidConsumerUsage;
using micropixel::HidKeySequence;
using micropixel::HidKeyUsage;
using micropixel::Rect;

constexpr Color kBackground = Color::Rgb(16U, 18U, 24U);
constexpr Color kPanel = Color::Rgb(34U, 38U, 48U);
constexpr Color kText = Color::Rgb(236U, 238U, 242U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);
constexpr Color kWhatsApp = Color::Rgb(37U, 185U, 76U);
constexpr Color kWeChat = Color::Rgb(9U, 149U, 248U);
constexpr Color kVolumeDown = Color::Rgb(160U, 164U, 250U);
constexpr Color kVolumeUp = Color::Rgb(250U, 212U, 0U);

enum class Tile : uint8_t { kWhatsApp, kWeChat, kVolumeDown, kVolumeUp };
constexpr uint32_t kTileCount = 4U;

class ChatRemoteApp final {
   public:
    explicit ChatRemoteApp(micropixel::Application& app)
        : app_(app),
          keyboard_(app.hid_keyboard()),
          sequence_(app.hid_keyboard(), app.timers()),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          poll_(app.timers().Every(micropixel::Duration::Milliseconds(500U)).value()) {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        const int32_t margin = width * 3 / 100;
        const int32_t gap = margin;
        (void)scene_.CreateLabel({margin, height * 3 / 100}, "Chat Remote", kText, micropixel::SystemFont::kTitle)
            .value();
        link_label_ = scene_.CreateLabel({width * 70 / 100, height * 5 / 100}, "PAIR", kWarn).value();

        const Rect panel{margin, height * 17 / 100, width - margin * 2, height * 13 / 100};
        (void)scene_.CreateRoundedRect(panel, Style(kPanel, 18U)).value();
        status_ = scene_.CreateLabel({margin * 2, panel.y + panel.height / 4}, "", kText).value();

        const int32_t top = panel.y + panel.height + gap;
        const int32_t tile_width = (width - margin * 2 - gap) / 2;
        const int32_t tile_height = (height - top - margin - gap) / 2;
        constexpr std::array<const char*, kTileCount> kLabels{"WhatsApp", "WeChat", "Vol -", "Vol +"};
        constexpr std::array<Color, kTileCount> kColors{kWhatsApp, kWeChat, kVolumeDown, kVolumeUp};
        for (uint32_t index = 0U; index < kTileCount; ++index) {
            const int32_t column = static_cast<int32_t>(index % 2U);
            const int32_t row = static_cast<int32_t>(index / 2U);
            tiles_[index] = {margin + column * (tile_width + gap), top + row * (tile_height + gap), tile_width,
                             tile_height};
            tile_nodes_[index] = scene_.CreateRoundedRect(tiles_[index], Style(kColors[index], 24U)).value();
            const auto measured = app_.renderer().MeasureText(kLabels[index], micropixel::SystemFont::kLarge);
            const int32_t text_width = measured ? static_cast<int32_t>(measured->width) : 0;
            const int32_t text_height = measured ? static_cast<int32_t>(measured->height) : 0;
            (void)scene_
                .CreateLabel({tiles_[index].x + (tile_width - text_width) / 2,
                              tiles_[index].y + (tile_height - text_height) / 2},
                             kLabels[index], Color::Black(), micropixel::SystemFont::kLarge)
                .value();
        }

        if (!keyboard_.Start()) {
            app_.log().Error("Bluetooth keyboard unavailable");
        }
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        const auto sequence = sequence_.Handle(event);
        if (sequence != HidKeySequence::Status::kIdle) {
            Finished(sequence);
        } else if (event.TimerFrom(poll_) != nullptr || event.type() == micropixel::EventType::kResume) {
            Refresh();
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kDown) {
                for (uint32_t index = 0U; index < kTileCount; ++index) {
                    if (tiles_[index].contains(touch->position())) {
                        Trigger(static_cast<Tile>(index));
                    }
                }
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown || key->phase() == micropixel::KeyPhase::kRepeat) {
                if (key->code() == micropixel::KeyCode::kLeft) {
                    Trigger(Tile::kVolumeDown);
                } else if (key->code() == micropixel::KeyCode::kRight) {
                    Trigger(Tile::kVolumeUp);
                }
            }
        } else if (event.type() == micropixel::EventType::kStop) {
            sequence_.Cancel();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    static micropixel::RoundedRectStyle Style(Color fill, uint32_t radius) {
        micropixel::RoundedRectStyle style{};
        style.fill = fill;
        style.stroke = Color::White();
        style.radius = radius;
        style.stroke_width = 3U;
        return style;
    }

    void Trigger(Tile tile) {
        if (sequence_.running()) {
            return;
        }
        if (!connected_) {
            Sound(chat_remote_sfx::kFail);
            SetStatus("Bluetooth not connected");
            return;
        }
        Sound(chat_remote_sfx::kPress);
        bool queued = false;
        if (tile == Tile::kWhatsApp || tile == Tile::kWeChat) {
            // Spotlight: Command-Space, type the app name, Return.
            // Each key gets its own report with a pause so macOS sees Command
            // held before Space, and Spotlight has time to open before typing.
            queued = sequence_.Key(HidKeyUsage::kLeftGui, true, Duration::Milliseconds(60U)) &&
                     sequence_.Key(HidKeyUsage::kSpace, true, Duration::Milliseconds(60U)) &&
                     sequence_.Key(HidKeyUsage::kSpace, false, Duration::Milliseconds(40U)) &&
                     sequence_.Key(HidKeyUsage::kLeftGui, false, Duration::Milliseconds(600U)) &&
                     sequence_.Type(tile == Tile::kWhatsApp ? "whatsap" : "wecha", Duration::Milliseconds(25U)) &&
                     // Last letter, then let Spotlight settle on the app before Return.
                     sequence_.Key(micropixel::HidLetter(tile == Tile::kWhatsApp ? 'p' : 't'), true,
                                   Duration::Milliseconds(25U)) &&
                     sequence_.Key(micropixel::HidLetter(tile == Tile::kWhatsApp ? 'p' : 't'), false,
                                   Duration::Milliseconds(400U)) &&
                     sequence_.Chord({HidKeyUsage::kEnter}, Duration::Milliseconds(40U));
            SetStatus(tile == Tile::kWhatsApp ? "Opening WhatsApp" : "Opening WeChat");
            done_status_ = tile == Tile::kWhatsApp ? "Sent WhatsApp" : "Sent WeChat";
        } else {
            const auto usage = tile == Tile::kVolumeUp ? HidConsumerUsage::kVolumeUp : HidConsumerUsage::kVolumeDown;
            queued = sequence_.Consumer(usage, true, Duration::Milliseconds(60U)) && sequence_.Consumer(usage, false);
            SetStatus(tile == Tile::kVolumeUp ? "Volume up" : "Volume down");
            done_status_ = nullptr;
        }
        if (queued) {
            Finished(sequence_.Start());
        }
    }

    void Finished(HidKeySequence::Status status) {
        if (status == HidKeySequence::Status::kFailed) {
            Sound(chat_remote_sfx::kFail);
            SetStatus("Bluetooth not connected");
        } else if (status == HidKeySequence::Status::kDone && done_status_ != nullptr) {
            Sound(chat_remote_sfx::kRelease);
            SetStatus(done_status_);
        }
    }

    void Refresh() {
        const auto state = keyboard_.State();
        const bool available = state && state->available;
        const bool connected = available && state->connected;
        if (connected != connected_ || !shown_once_) {
            connected_ = connected;
            shown_once_ = true;
            link_label_.SetText(connected ? "BLE ON" : "PAIR");
            link_label_.SetColor(connected ? kGood : kWarn);
            if (!sequence_.running()) {
                SetStatus(!available ? "Bluetooth unavailable"
                                     : (connected ? "Connected to Mac" : "Pair \"CGotchi\" on your Mac"));
            }
        }
        Present();
    }

    void SetStatus(const char* text) {
        status_.SetText(text);
        Present();
    }

    // Audible confirmation for every press, so input is never silent.
    void Sound(std::span<const micropixel::ToneSpec> effect) { (void)tones_.Play(effect); }

    void Present() { (void)app_.renderer().Present(scene_); }

    micropixel::Application& app_;
    micropixel::HidKeyboard keyboard_;
    HidKeySequence sequence_;
    micropixel::ToneSequencer<2U> tones_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer poll_;
    micropixel::LabelNode link_label_;
    micropixel::LabelNode status_;
    std::array<Rect, kTileCount> tiles_{};
    std::array<micropixel::RoundedRectNode, kTileCount> tile_nodes_{};
    const char* done_status_{};
    bool connected_{};
    bool shown_once_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    ChatRemoteApp remote(app);
    app.log().Info("Chat Remote ready");
    app.Run([&remote](const micropixel::Event& event) { return remote.Handle(event); });
    return 0;
}
