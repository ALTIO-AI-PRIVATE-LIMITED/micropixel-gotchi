// SPDX-License-Identifier: Apache-2.0
//
// WisprKey Pro: one big button per Wispr Flow action. Swipe left or right to
// move between the circles, then press the circle to run its shortcut on the
// paired Mac. The Gotchi is a Bluetooth keyboard ("CGotchi").
//
// Wispr Flow binds Command Mode to Ctrl+Fn, and a generic Bluetooth keyboard
// cannot send Fn. That circle presses Ctrl+F24 and relies on the WisprKey
// Karabiner-Elements rule (matched by this keyboard's VID/PID) to turn F24
// into Fn. Every other circle is a plain shortcut that works without
// Karabiner; talking uses Wispr's Control+Option push-to-talk binding.
//
// Controls: swipe to change action; tap the circle, or hold it for "hold"
// actions. VOL- sends Enter, VOL+ moves to the next circle and BOOT presses
// the circle.
#include <stdint.h>

#include <array>
#include <optional>
#include <span>

#include "sdk/micropixel.hpp"
#include "wispr-key_sfx_profiles.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::HidKeySequence;
using micropixel::HidKeyUsage;
using micropixel::Point;
using micropixel::Rect;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kDot = Color::Rgb(70U, 76U, 90U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);
constexpr Color kActive = Color::Rgb(240U, 72U, 88U);

constexpr HidKeyUsage kKeyE = micropixel::HidLetter('e');
constexpr HidKeyUsage kKeyC = micropixel::HidLetter('c');
constexpr HidKeyUsage kKeyM = micropixel::HidLetter('m');
constexpr HidKeyUsage kKeyS = micropixel::HidLetter('s');
constexpr HidKeyUsage kKeyV = micropixel::HidLetter('v');
constexpr HidKeyUsage kKeyZ = micropixel::HidLetter('z');
constexpr HidKeyUsage kNone = static_cast<HidKeyUsage>(0U);

enum class Press : uint8_t {
    kTap,        // Press and release the chord once.
    kHold,       // Hold the chord while the finger or BOOT is down.
    kDoubleTap,  // Two quick taps: Wispr's hands-free toggle on the talk key.
    kSelectAll,  // Cmd+A first: Wispr transforms act on the selected text.
};

struct Action final {
    const char* badge;   // Word inside the circle.
    const char* name;    // Caption under the circle.
    const char* detail;  // How to use it and the Mac shortcut.
    Color color;
    Press press;
    std::array<HidKeyUsage, 3U> chord;  // Modifiers first; kNone pads.
};

// Wispr Flow's Mac shortcuts, as configured in this Mac's Wispr settings.
constexpr std::array<Action, 11U> kActions{{
    {"TALK", "Hold to talk", "Hold the circle  |  Ctrl+Option", Color::Rgb(64U, 120U, 255U), Press::kHold,
     {HidKeyUsage::kLeftControl, HidKeyUsage::kLeftAlt}},
    {"FREE", "Hands-free", "Tap to start, tap to stop", Color::Rgb(0U, 170U, 160U), Press::kDoubleTap,
     {HidKeyUsage::kLeftControl, HidKeyUsage::kLeftAlt}},
    {"EDIT", "Command Mode", "Hold, say \"fix spelling\"  |  Ctrl+Fn", Color::Rgb(150U, 90U, 240U), Press::kHold,
     {HidKeyUsage::kLeftControl, HidKeyUsage::kF24}},
    {"POLISH", "Polish message", "Selects all, then Ctrl+E", Color::Rgb(236U, 120U, 40U), Press::kSelectAll,
     {HidKeyUsage::kLeftControl, kKeyE}},
    {"NOTES", "Scratchpad", "Open notes  |  Opt+S", Color::Rgb(240U, 190U, 30U), Press::kTap,
     {HidKeyUsage::kLeftAlt, kKeyS}},
    {"MEET", "Meeting notes", "Start Notetaker  |  Opt+M", Color::Rgb(220U, 80U, 160U), Press::kTap,
     {HidKeyUsage::kLeftAlt, kKeyM}},
    {"PASTE", "Paste last", "Last transcript  |  Cmd+Ctrl+V", Color::Rgb(60U, 180U, 90U), Press::kTap,
     {HidKeyUsage::kLeftGui, HidKeyUsage::kLeftControl, kKeyV}},
    {"COPY", "Copy last", "Last transcript  |  Cmd+Ctrl+C", Color::Rgb(70U, 160U, 210U), Press::kTap,
     {HidKeyUsage::kLeftGui, HidKeyUsage::kLeftControl, kKeyC}},
    {"UNDO", "Undo AI edit", "Cmd+Z", Color::Rgb(130U, 136U, 150U), Press::kTap, {HidKeyUsage::kLeftGui, kKeyZ}},
    {"SEND", "Send", "Enter", Color::Rgb(37U, 185U, 76U), Press::kTap, {HidKeyUsage::kEnter}},
    {"STOP", "Cancel", "Esc", Color::Rgb(200U, 60U, 60U), Press::kTap, {HidKeyUsage::kEscape}},
}};
constexpr uint32_t kActionCount = kActions.size();
// A finger must stay still this long on a "hold" circle before it presses.
constexpr uint64_t kHoldDelayMs = 180U;

class WisprKeyApp final {
   public:
    explicit WisprKeyApp(micropixel::Application& app)
        : app_(app),
          keyboard_(app.hid_keyboard()),
          sequence_(app.hid_keyboard(), app.timers()),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          poll_(app.timers().Every(Duration::Milliseconds(400U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        const int32_t margin = width_ * 3 / 100;
        title_ = scene_.CreateLabel({margin, height_ * 3 / 100}, "WisprKey Pro", kText, SystemFont::kMedium).value();
        link_ = scene_.CreateLabel({width_ * 74 / 100, height_ * 3 / 100}, "PAIR", kWarn).value();

        // Circles sit on one horizontal strip; neighbours peek in at the edges.
        diameter_ = height_ * 50 / 100;
        pitch_ = diameter_ + width_ * 14 / 100;
        circle_top_ = height_ * 14 / 100;
        strip_x_ = StripX(0);
        strip_ = scene_.CreateContainer({.translation = {strip_x_, 0}}).value();
        for (uint32_t index = 0U; index < kActionCount; ++index) {
            const Action& action = kActions[index];
            const Rect circle{static_cast<int32_t>(index) * pitch_ - diameter_ / 2, circle_top_, diameter_,
                              diameter_};
            micropixel::RoundedRectStyle style{};
            style.fill = action.color;
            style.stroke = Color::Mix(Color::White(), action.color, 90U);
            style.radius = static_cast<uint32_t>(diameter_);
            style.stroke_width = 6U;
            circles_[index] = strip_.CreateRoundedRect(circle, style).value();
            const auto measured = app_.renderer().MeasureText(action.badge, SystemFont::kTitle);
            const int32_t text_width = measured ? static_cast<int32_t>(measured->width) : 0;
            const int32_t text_height = measured ? static_cast<int32_t>(measured->height) : 0;
            (void)strip_
                .CreateLabel({circle.x + (diameter_ - text_width) / 2, circle.y + (diameter_ - text_height) / 2},
                             action.badge, Color::White(), SystemFont::kTitle)
                .value();
        }

        name_ = scene_.CreateLabel({0, height_ * 67 / 100}, "", kText, SystemFont::kLarge).value();
        detail_ = scene_.CreateLabel({0, height_ * 79 / 100}, "", kMuted, SystemFont::kSmall).value();
        const int32_t dot = height_ * 2 / 100;
        const int32_t dot_gap = dot * 2;
        const int32_t dots_width = static_cast<int32_t>(kActionCount) * dot_gap - (dot_gap - dot);
        for (uint32_t index = 0U; index < kActionCount; ++index) {
            micropixel::RoundedRectStyle style{};
            style.fill = kDot;
            style.radius = static_cast<uint32_t>(dot);
            dots_[index] = scene_
                               .CreateRoundedRect({(width_ - dots_width) / 2 + static_cast<int32_t>(index) * dot_gap,
                                                   height_ * 91 / 100, dot, dot},
                                                  style)
                               .value();
        }

        if (!keyboard_.Start()) {
            app_.log().Error("Bluetooth keyboard unavailable");
        }
        ShowSelection();
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        const auto sequence = sequence_.Handle(event);
        if (sequence != HidKeySequence::Status::kIdle) {
            Finished(sequence);
        } else if (hold_delay_ && event.TimerFrom(*hold_delay_) != nullptr) {
            hold_delay_.reset();
            if (!swiping_) {
                BeginHold();
            }
        } else if (animation_ && event.TimerFrom(*animation_) != nullptr) {
            Animate();
        } else if (event.TimerFrom(poll_) != nullptr || event.type() == micropixel::EventType::kResume) {
            Refresh();
        } else if (const auto* touch = event.touch()) {
            HandleTouch(*touch);
        } else if (const auto* key = event.key()) {
            HandleKey(*key);
        } else if (event.type() == micropixel::EventType::kStop) {
            EndHold();
            sequence_.Cancel();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    // Strip translation that centres circle `index`.
    [[nodiscard]] int32_t StripX(uint32_t index) const {
        return width_ / 2 - static_cast<int32_t>(index) * pitch_;
    }

    [[nodiscard]] bool OnCircle(Point point) const {
        const int32_t dx = point.x - width_ / 2;
        const int32_t dy = point.y - (circle_top_ + diameter_ / 2);
        const int32_t radius = diameter_ * 6 / 10;  // A little generous for fingers.
        return dx * dx + dy * dy <= radius * radius;
    }

    void HandleTouch(const micropixel::TouchEvent& touch) {
        const int32_t threshold = width_ / 25;
        switch (touch.phase()) {
            case micropixel::TouchPhase::kDown:
                touch_start_ = touch.position();
                swiping_ = false;
                pressed_circle_ = OnCircle(touch.position());
                if (pressed_circle_ && kActions[selected_].press == Press::kHold) {
                    // Wait briefly before holding, so a swipe that starts on
                    // the circle never presses it.
                    auto timer = app_.timers().After(Duration::Milliseconds(kHoldDelayMs));
                    if (timer) {
                        hold_delay_.emplace(static_cast<micropixel::Timer&&>(*timer));
                    }
                }
                break;
            case micropixel::TouchPhase::kMove: {
                const int32_t dx = touch.x() - touch_start_.x;
                if (!swiping_ && (dx > threshold / 2 || dx < -threshold / 2)) {
                    hold_delay_.reset();  // Moving: this touch is a swipe, not a hold.
                }
                if (!swiping_ && (dx > threshold || dx < -threshold)) {
                    swiping_ = true;
                    EndHold();  // A swipe changes action; it never presses.
                    animation_.reset();
                }
                if (swiping_) {
                    strip_x_ = StripX(selected_) + dx;
                    strip_.SetTranslation({strip_x_, 0});
                    Present();
                }
                break;
            }
            case micropixel::TouchPhase::kUp: {
                const int32_t dx = touch.x() - touch_start_.x;
                if (swiping_) {
                    const int32_t step = dx < -pitch_ / 4 ? 1 : (dx > pitch_ / 4 ? -1 : 0);
                    Select(static_cast<int32_t>(selected_) + step);
                } else if (pressed_circle_ && kActions[selected_].press != Press::kHold) {
                    Fire();
                }
                hold_delay_.reset();
                EndHold();
                swiping_ = false;
                break;
            }
            case micropixel::TouchPhase::kCancel:
                hold_delay_.reset();
                EndHold();
                if (swiping_) {
                    Select(static_cast<int32_t>(selected_));
                }
                swiping_ = false;
                break;
        }
    }

    void HandleKey(const micropixel::KeyEvent& key) {
        const bool down = key.phase() == micropixel::KeyPhase::kDown;
        const bool repeat = key.phase() == micropixel::KeyPhase::kRepeat;
        if (key.code() == micropixel::KeyCode::kLeft && down) {
            SendEnter();  // VOL-: send the dictated message.
        } else if (key.code() == micropixel::KeyCode::kRight && (down || repeat)) {
            // VOL+: next circle, wrapping round to the first.
            Select(selected_ + 1U < kActionCount ? static_cast<int32_t>(selected_) + 1 : 0);
        } else if (key.code() == micropixel::KeyCode::kConfirm && down) {
            if (kActions[selected_].press == Press::kHold) {
                BeginHold();
            } else {
                Fire();
            }
        } else if (key.code() == micropixel::KeyCode::kConfirm && !repeat) {
            EndHold();
        }
    }

    // Moves to circle `index` (clamped) and slides the strip there.
    void Select(int32_t index) {
        const int32_t last = static_cast<int32_t>(kActionCount) - 1;
        selected_ = static_cast<uint32_t>(index < 0 ? 0 : (index > last ? last : index));
        ShowSelection();
        if (!animation_) {
            auto timer = app_.timers().Every(Duration::Milliseconds(16U));
            if (timer) {
                animation_.emplace(static_cast<micropixel::Timer&&>(*timer));
            }
        }
        if (!animation_) {
            strip_x_ = StripX(selected_);
            strip_.SetTranslation({strip_x_, 0});
        }
        Present();
    }

    // Eases the strip toward the selected circle, one frame per tick.
    void Animate() {
        const int32_t target = StripX(selected_);
        const int32_t delta = target - strip_x_;
        strip_x_ += delta / 3 != 0 ? delta / 3 : delta;
        strip_.SetTranslation({strip_x_, 0});
        if (strip_x_ == target) {
            animation_.reset();
        }
        Present();
    }

    void ShowSelection() {
        const Action& action = kActions[selected_];
        CenterLabel(name_, action.name, SystemFont::kLarge, height_ * 67 / 100);
        CenterLabel(detail_, action.detail, SystemFont::kSmall, height_ * 79 / 100);
        for (uint32_t index = 0U; index < kActionCount; ++index) {
            circles_[index].SetOpacity(index == selected_ ? 255U : 110U);
            dots_[index].SetFillColor(index == selected_ ? kText : kDot);
        }
        PaintActive();
    }

    void CenterLabel(micropixel::LabelNode& label, const char* text, SystemFont font, int32_t y) {
        const auto measured = app_.renderer().MeasureText(text, font);
        const int32_t text_width = measured ? static_cast<int32_t>(measured->width) : 0;
        label.SetText(text);
        label.SetPosition({(width_ - text_width) / 2, y});
    }

    // The pressed circle turns red until its keys are released.
    void PaintActive() {
        const bool active = holding_ || firing_;
        const Color fill = active ? kActive : kActions[selected_].color;
        circles_[selected_].SetFillColor(fill);
        circles_[selected_].SetStrokeColor(active ? Color::White() : Color::Mix(Color::White(), fill, 90U));
    }

    void BeginHold() {
        if (holding_ || sequence_.running()) {
            return;
        }
        if (!connected_) {
            Sound(wispr_key_sfx::kFail);
            return;
        }
        Sound(wispr_key_sfx::kPress);
        holding_ = selected_;
        for (HidKeyUsage usage : kActions[selected_].chord) {
            if (usage != kNone) {
                (void)keyboard_.Key(usage, true);
            }
        }
        PaintActive();
        Present();
    }

    void EndHold() {
        if (!holding_) {
            return;
        }
        const auto& chord = kActions[*holding_].chord;
        for (uint32_t index = chord.size(); index > 0U; --index) {
            if (chord[index - 1U] != kNone) {
                (void)keyboard_.Key(chord[index - 1U], false);
            }
        }
        holding_.reset();
        Sound(wispr_key_sfx::kRelease);
        PaintActive();
        Present();
    }

    void Fire() {
        if (holding_ || sequence_.running()) {
            return;
        }
        if (!connected_) {
            Sound(wispr_key_sfx::kFail);
            return;
        }
        const Action& action = kActions[selected_];
        const uint32_t taps = action.press == Press::kDoubleTap ? 2U : 1U;
        bool queued = true;
        if (action.press == Press::kSelectAll) {
            queued = sequence_.Chord({HidKeyUsage::kLeftGui, micropixel::HidLetter('a')}, Duration::Milliseconds(40U),
                                     Duration::Milliseconds(150U));
        }
        for (uint32_t tap = 0U; tap < taps; ++tap) {
            const auto [first, second, third] = action.chord;
            const Duration hold = Duration::Milliseconds(40U);
            const Duration gap = Duration::Milliseconds(tap + 1U < taps ? 90U : 0U);
            queued = queued && (third != kNone    ? sequence_.Chord({first, second, third}, hold, gap)
                                : second != kNone ? sequence_.Chord({first, second}, hold, gap)
                                                  : sequence_.Chord({first}, hold, gap));
        }
        if (!queued) {
            return;
        }
        Sound(wispr_key_sfx::kPress);
        firing_ = true;
        PaintActive();
        Present();
        Finished(sequence_.Start());
    }

    void SendEnter() {
        if (holding_ || sequence_.running()) {
            return;
        }
        if (!connected_) {
            Sound(wispr_key_sfx::kFail);
            return;
        }
        if (sequence_.Chord({HidKeyUsage::kEnter}, Duration::Milliseconds(40U))) {
            Sound(wispr_key_sfx::kPress);
            Finished(sequence_.Start());
        }
    }

    void Finished(HidKeySequence::Status status) {
        if (status == HidKeySequence::Status::kRunning) {
            return;
        }
        if (status == HidKeySequence::Status::kFailed) {
            app_.log().Warning("shortcut was not delivered");
            Sound(wispr_key_sfx::kFail);
        }
        firing_ = false;
        PaintActive();
        Present();
    }

    void Refresh() {
        const auto state = keyboard_.State();
        const bool available = state && state->available;
        const bool connected = available && state->connected;
        if (connected != connected_ || !refreshed_) {
            connected_ = connected;
            refreshed_ = true;
            if (!connected_) {
                holding_.reset();  // The Host drops held keys with the link.
                PaintActive();
            }
            link_.SetText(!available ? "NO BLE" : (connected ? "CONNECTED" : "PAIR"));
            // Until paired, the title names the device to pick on the Mac.
            title_.SetText(connected || !available ? "WisprKey Pro" : "Mac Bluetooth: CGotchi");
            title_.SetColor(connected || !available ? kText : kWarn);
            link_.SetColor(connected ? kGood : kWarn);
            Present();
        }
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
    std::optional<micropixel::Timer> animation_;
    std::optional<micropixel::Timer> hold_delay_;
    micropixel::LabelNode title_;
    micropixel::LabelNode link_;
    micropixel::LabelNode name_;
    micropixel::LabelNode detail_;
    micropixel::ContainerNode strip_;
    std::array<micropixel::RoundedRectNode, kActionCount> circles_{};
    std::array<micropixel::RoundedRectNode, kActionCount> dots_{};
    int32_t width_{};
    int32_t height_{};
    int32_t diameter_{};
    int32_t pitch_{};
    int32_t circle_top_{};
    int32_t strip_x_{};
    Point touch_start_{};
    bool swiping_{};
    bool pressed_circle_{};
    bool firing_{};
    std::optional<uint32_t> holding_;
    uint32_t selected_{};
    bool connected_{};
    bool refreshed_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    WisprKeyApp wispr_key(app);
    app.log().Info("WisprKey Pro ready");
    app.Run([&wispr_key](const micropixel::Event& event) { return wispr_key.Handle(event); });
    return 0;
}
