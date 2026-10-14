// SPDX-License-Identifier: Apache-2.0
//
// Focus Timer: a Pomodoro timer with the Gotchi Pet face. Tap to start a focus
// session; the face concentrates while it counts down. When it ends, tap to
// take your break. Skip the break and the face gets annoyed and grumbles
// until you take it. VOL-/VOL+ pick the focus length while the timer is idle;
// VOL- also resets a paused timer. Every fourth break is a long one.
#include <stdint.h>

#include <span>

#include "focus-timer_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

namespace mp = micropixel;
namespace math = micropixel::math;
using mp::Color;
using mp::Rect;

constexpr Color kBackground = Color::Rgb(8U, 8U, 12U);
constexpr Color kEye = Color::Rgb(120U, 236U, 255U);
constexpr Color kBreakEye = Color::Rgb(140U, 240U, 140U);
constexpr Color kAngryEye = Color::Rgb(255U, 80U, 70U);
constexpr Color kText = Color::Rgb(236U, 238U, 242U);
constexpr Color kDim = Color::Rgb(130U, 136U, 150U);
constexpr Color kTrack = Color::Rgb(40U, 44U, 56U);
constexpr Color kTomato = Color::Rgb(255U, 96U, 64U);

constexpr uint64_t kSecond = 1'000'000U;
constexpr uint64_t kMinute = 60U * kSecond;
constexpr uint32_t kFocusChoices[] = {15U, 25U, 45U, 60U};
constexpr uint32_t kFocusChoiceCount = sizeof(kFocusChoices) / sizeof(kFocusChoices[0]);
constexpr uint64_t kShortBreakUs = 5U * kMinute;
constexpr uint64_t kLongBreakUs = 15U * kMinute;
constexpr uint32_t kSessionsPerLongBreak = 4U;
constexpr uint64_t kAnnoyedAfterUs = 60U * kSecond;  // break offered but not taken
constexpr uint64_t kGrumbleEveryUs = 30U * kSecond;
constexpr uint64_t kBlinkUs = 160U * 1000U;

enum class Phase : uint8_t { kReady, kFocus, kBreakDue, kBreak };

float Wave(uint64_t elapsed_us, float hertz) {
    return math::Sin(static_cast<float>(elapsed_us % (10U * kSecond)) * 1e-6F * hertz * math::kTwoPi);
}

class FocusTimer final {
   public:
    explicit FocusTimer(mp::Application& app)
        : app_(app),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          frame_(app.timers().Every(mp::Duration::Milliseconds(50U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        cx_ = width_ / 2;

        mp::RoundedRectStyle eye{};
        eye.fill = kEye;
        mp::RoundedRectStyle lid{};
        lid.fill = kBackground;
        for (uint32_t index = 0U; index < 2U; ++index) {
            eyes_[index] = scene_.CreateRoundedRect({}, eye).value();
            lids_[index] = scene_.CreateRoundedRect({}, lid).value();
            lids_[index].SetVisible(false);
        }
        time_ = scene_.CreateLabel({cx_, height_ * 58 / 100}, "", kText, mp::SystemFont::kTitle, true).value();
        hint_ = scene_.CreateLabel({cx_, height_ * 74 / 100}, "", kDim, mp::SystemFont::kMedium, true).value();

        mp::RoundedRectStyle track{};
        track.fill = kTrack;
        track.radius = 3U;
        progress_track_ = {width_ * 12 / 100, height_ - 34, width_ * 76 / 100, 6};
        (void)scene_.CreateRoundedRect(progress_track_, track).value();
        mp::RoundedRectStyle fill = track;
        fill.fill = kEye;
        progress_ = scene_.CreateRoundedRect(progress_track_, fill).value();

        mp::RoundedRectStyle dot{};
        dot.fill = kTrack;
        dot.radius = 5U;
        for (uint32_t index = 0U; index < kSessionsPerLongBreak; ++index) {
            const int32_t x = cx_ - 36 + static_cast<int32_t>(index) * 20;
            dots_[index] = scene_.CreateRoundedRect({x, height_ - 20, 10, 10}, dot).value();
        }

        choice_ = math::Min(app_.storage().GetU32Or("focus", 1U), kFocusChoiceCount - 1U);
        cycle_ = app_.storage().GetU32Or("cycle", 0U) % kSessionsPerLongBreak;
        EnterPhase(Phase::kReady, Now());
        Frame(Now());
    }

    mp::EventResult Handle(const mp::Event& event) {
        const uint64_t now = Now();
        if (event.TimerFrom(frame_) != nullptr) {
            Frame(now);
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == mp::TouchPhase::kDown) {
                Tap(now);
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == mp::KeyPhase::kDown) {
                if (key->code() == mp::KeyCode::kLeft) {
                    Step(-1, now);
                } else if (key->code() == mp::KeyCode::kRight) {
                    Step(1, now);
                } else {
                    Tap(now);
                }
            }
        }
        return mp::EventResult::kContinue;
    }

   private:
    [[nodiscard]] uint64_t Now() const { return app_.clock().Now().microseconds(); }

    [[nodiscard]] uint64_t FocusUs() const { return static_cast<uint64_t>(kFocusChoices[choice_]) * kMinute; }

    [[nodiscard]] bool Running() const { return phase_ == Phase::kFocus || phase_ == Phase::kBreak; }

    [[nodiscard]] uint64_t Elapsed(uint64_t now) const {
        return paused_ ? paused_elapsed_us_ : paused_elapsed_us_ + (now - started_us_);
    }

    void EnterPhase(Phase phase, uint64_t now) {
        phase_ = phase;
        phase_started_us_ = now;
        started_us_ = now;
        paused_elapsed_us_ = 0U;
        paused_ = false;
        last_grumble_us_ = now;
        switch (phase) {
            case Phase::kReady:
                duration_us_ = FocusUs();
                break;
            case Phase::kFocus:
                duration_us_ = FocusUs();
                break;
            case Phase::kBreakDue:
                duration_us_ = 0U;
                break;
            case Phase::kBreak:
                duration_us_ = cycle_ == 0U ? kLongBreakUs : kShortBreakUs;
                break;
        }
    }

    void Tap(uint64_t now) {
        Sound(focus_timer_sfx::kTap);
        switch (phase_) {
            case Phase::kReady:
                EnterPhase(Phase::kFocus, now);
                break;
            case Phase::kBreakDue:
                EnterPhase(Phase::kBreak, now);
                break;
            case Phase::kFocus:
            case Phase::kBreak:
                if (paused_) {
                    started_us_ = now;
                    paused_ = false;
                } else {
                    paused_elapsed_us_ = Elapsed(now);
                    paused_ = true;
                }
                break;
        }
    }

    void Step(int32_t direction, uint64_t now) {
        if (phase_ == Phase::kReady) {
            const int32_t next = math::Clamp(static_cast<int32_t>(choice_) + direction, 0,
                                             static_cast<int32_t>(kFocusChoiceCount) - 1);
            choice_ = static_cast<uint32_t>(next);
            (void)app_.storage().SetU32("focus", choice_);
            duration_us_ = FocusUs();
            Sound(focus_timer_sfx::kTap);
        } else if (direction < 0 && paused_) {
            Sound(focus_timer_sfx::kTap);
            EnterPhase(Phase::kReady, now);
        }
    }

    void Frame(uint64_t now) {
        if (Running() && Elapsed(now) >= duration_us_) {
            Sound(focus_timer_sfx::kDone);
            if (phase_ == Phase::kFocus) {
                cycle_ = (cycle_ + 1U) % kSessionsPerLongBreak;
                (void)app_.storage().SetU32("cycle", cycle_);
                (void)app_.storage().SetU32("total", app_.storage().GetU32Or("total", 0U) + 1U);
                EnterPhase(Phase::kBreakDue, now);
            } else {
                EnterPhase(Phase::kReady, now);
            }
        }
        if (phase_ == Phase::kBreakDue && now - phase_started_us_ >= kAnnoyedAfterUs &&
            now - last_grumble_us_ >= kGrumbleEveryUs) {
            last_grumble_us_ = now;
            Sound(focus_timer_sfx::kGrumble);
        }
        if (now >= next_blink_us_) {
            blink_started_us_ = now;
            next_blink_us_ = now + 2U * kSecond + static_cast<uint64_t>(app_.random().Below(4000U)) * 1000U;
        }
        Draw(now);
        (void)app_.renderer().Present(scene_);
    }

    void Draw(uint64_t now) {
        const uint64_t t = now - phase_started_us_;
        const bool annoyed = phase_ == Phase::kBreakDue && t >= kAnnoyedAfterUs;

        float open = 1.0F;
        if (now - blink_started_us_ < kBlinkUs) {
            const float half = static_cast<float>(kBlinkUs) * 0.5F;
            open = math::Abs(static_cast<float>(now - blink_started_us_) - half) / half;
        }

        // Face: focused eyes look down at the work, break eyes are relaxed
        // smiles, a skipped break gets red eyes with heavy flat lids.
        int32_t eye_w = 44;
        int32_t eye_h = static_cast<int32_t>(56.0F * open);
        int32_t look_x = 0;
        int32_t look_y = 0;
        Color color = kEye;
        bool lids = false;
        const char* hint = "";
        switch (phase_) {
            case Phase::kReady:
                hint = "Tap to focus  -/+ length";
                look_x = static_cast<int32_t>(Wave(now, 0.15F) * 10.0F);
                break;
            case Phase::kFocus:
                eye_h = static_cast<int32_t>(34.0F * open);
                look_y = 8;
                look_x = static_cast<int32_t>(Wave(now, 0.05F) * 6.0F);
                hint = paused_ ? "Paused. Tap to resume" : "Focusing...";
                break;
            case Phase::kBreakDue:
                if (annoyed) {
                    color = kAngryEye;
                    lids = true;
                    look_x = static_cast<int32_t>(Wave(t, 3.0F) * 3.0F);  // trembling with annoyance
                    hint = "Take your break!";
                } else {
                    eye_h = 18;
                    look_y = -6;
                    color = kBreakEye;
                    hint = "Done! Tap for a break";
                }
                break;
            case Phase::kBreak:
                eye_h = 18;
                look_y = -4 + static_cast<int32_t>(Wave(t, 0.5F) * 3.0F);
                color = kBreakEye;
                hint = paused_ ? "Paused. Tap to resume" : "Break. Stretch a bit";
                break;
        }
        if (paused_) {
            color = kDim;
        }
        eye_h = math::Max(eye_h, 6);

        const int32_t eye_cy = height_ * 26 / 100 + look_y;
        for (uint32_t index = 0U; index < 2U; ++index) {
            const int32_t center_x = cx_ + (index == 0U ? -40 : 40) + look_x;
            const Rect eye{center_x - eye_w / 2, eye_cy - eye_h / 2, eye_w, eye_h};
            eyes_[index].SetRect(eye);
            eyes_[index].SetRadius(static_cast<uint32_t>(math::Min(eye_w, eye_h) / 2));
            eyes_[index].SetFillColor(color);
            // The lid covers the top third, shifted toward the middle, for a frown.
            lids_[index].SetRect({eye.x + (index == 0U ? 8 : -8), eye.y - 6, eye.width, eye.height / 3 + 6});
            lids_[index].SetVisible(lids);
        }

        uint64_t remaining = 0U;
        if (phase_ == Phase::kReady) {
            remaining = FocusUs();
        } else if (Running()) {
            remaining = duration_us_ - math::Min(Elapsed(now), duration_us_);
        }
        if (phase_ == Phase::kBreakDue) {
            time_.SetText(annoyed ? ">:(" : "Break!");
        } else {
            SetTime(remaining);
        }
        hint_.SetText(hint);

        Rect bar = progress_track_;
        if (Running() && duration_us_ > 0U) {
            const uint64_t done = math::Min(Elapsed(now), duration_us_);
            bar.width = math::Max(bar.height, static_cast<int32_t>(static_cast<uint64_t>(bar.width) * done / duration_us_));
        } else {
            bar.width = phase_ == Phase::kBreakDue ? bar.width : bar.height;
        }
        progress_.SetRect(bar);
        progress_.SetFillColor(phase_ == Phase::kBreak || phase_ == Phase::kBreakDue ? kBreakEye : kEye);

        for (uint32_t index = 0U; index < kSessionsPerLongBreak; ++index) {
            // cycle_ counts finished sessions; 0 right after the fourth means all four are done.
            const bool finished = index < cycle_ || (cycle_ == 0U && phase_ != Phase::kReady &&
                                                     phase_ != Phase::kFocus);
            dots_[index].SetFillColor(finished ? kTomato : kTrack);
        }
    }

    void SetTime(uint64_t remaining_us) {
        const uint32_t seconds = static_cast<uint32_t>((remaining_us + kSecond - 1U) / kSecond);
        const uint32_t minutes = seconds / 60U;
        const uint32_t rest = seconds % 60U;
        char text[8] = {};
        text[0] = static_cast<char>('0' + (minutes / 10U) % 10U);
        text[1] = static_cast<char>('0' + minutes % 10U);
        text[2] = ':';
        text[3] = static_cast<char>('0' + rest / 10U);
        text[4] = static_cast<char>('0' + rest % 10U);
        time_.SetText(text);
    }

    void Sound(std::span<const mp::ToneSpec> effect) { (void)tones_.Play(effect); }

    mp::Application& app_;
    mp::ToneSequencer<4U> tones_;
    mp::RendererInfo info_;
    mp::Scene scene_;
    mp::Timer frame_;
    mp::RoundedRectNode eyes_[2];
    mp::RoundedRectNode lids_[2];
    mp::LabelNode time_;
    mp::LabelNode hint_;
    mp::RoundedRectNode progress_;
    mp::RoundedRectNode dots_[kSessionsPerLongBreak];
    Rect progress_track_{};
    int32_t width_{};
    int32_t height_{};
    int32_t cx_{};

    Phase phase_{Phase::kReady};
    uint32_t choice_{};
    uint32_t cycle_{};
    bool paused_{};
    uint64_t duration_us_{};
    uint64_t phase_started_us_{};
    uint64_t started_us_{};
    uint64_t paused_elapsed_us_{};
    uint64_t last_grumble_us_{};
    uint64_t blink_started_us_{};
    uint64_t next_blink_us_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    FocusTimer timer(app);
    app.log().Info("Focus Timer ready");
    app.Run([&timer](const micropixel::Event& event) { return timer.Handle(event); });
    return 0;
}
