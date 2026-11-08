// SPDX-License-Identifier: MIT
//
// Slot Machine for the Gotchi. Ported from M5StickC_Plus_Slot_Machine by
// Sarah-C (https://github.com/Sarah-C/M5StickC_Plus_Slot_Machine), MIT.
// Keeps the original reel layout, spin physics and round flow. The symbols are
// redrawn as shapes. Play money only.
//
// Controls: tap or BOOT to spin, then again to stop each reel in turn.
#include "apps/portkit/portkit.hpp"

namespace {

using micropixel::Application;
using portkit::Canvas;
using portkit::Color;
using portkit::Rgb;

constexpr int32_t kReels = 3, kReelLength = 9;
constexpr int32_t kSymbols[kReelLength] = {2, 4, 5, 0, 3, 4, 1, 5, 3};
constexpr float kStep = 360.0F / kReelLength;
constexpr int32_t kSpacing = 66, kWindowTop = 36, kWindowBottom = 186, kReelW = 86;
constexpr int32_t kPayout[6] = {25, 15, 10, 5, 3, 2};

const Color kInk = Rgb(245, 245, 240), kGold = Rgb(255, 196, 60), kRed = Rgb(226, 54, 62), kGreen = Rgb(74, 190, 96);

struct Reel final {
    float angle{}, velocity{}, acceleration{};
    bool spinning{}, stopping{};
    float min_velocity{}, max_velocity{};

    void Start() {
        spinning = true; stopping = false;
        acceleration = 12.0F; max_velocity = 720.0F;
    }
    void Stop() { stopping = true; acceleration = -20.0F; min_velocity = 50.0F; }
    void Update(float seconds) {
        if (!spinning) { return; }
        velocity = micropixel::math::Clamp(velocity + acceleration * seconds * 60.0F, min_velocity, max_velocity);
        const float before = angle;
        angle += velocity * seconds;
        if (stopping && velocity <= min_velocity) {
            const int32_t slot = static_cast<int32_t>(before / kStep);
            const float target = static_cast<float>(slot + 1) * kStep;
            if (angle >= target) {
                angle = target >= 360.0F ? target - 360.0F : target;
                velocity = 0.0F;
                spinning = false;
            }
        }
        if (angle >= 360.0F) { angle -= 360.0F; }
    }
    [[nodiscard]] int32_t Center() const {
        const int32_t slot = micropixel::math::RoundToInt(angle / kStep) % kReelLength;
        return kSymbols[(kReelLength - slot) % kReelLength];
    }
};

class SlotMachine final {
   public:
    explicit SlotMachine(Application& app) : app_(app), canvas_(app, Rgb(26, 16, 46), 1700U, 10U) { Draw(); }

    void Tick(uint32_t ms) {
        const float seconds = static_cast<float>(ms) / 1000.0F;
        bool any = false;
        for (auto& reel : reels_) { reel.Update(seconds); any = any || reel.spinning; }
        if (spinning_ && !any && stopped_ == kReels) { Finish(); }
        if (flash_ms_ > 0U) { flash_ms_ = flash_ms_ > ms ? flash_ms_ - ms : 0U; }
        Draw();
    }

    void Touch(const micropixel::TouchEvent& touch) {
        if (touch.phase() == micropixel::TouchPhase::kDown) { Press(); }
    }
    void Key(const micropixel::KeyEvent& key) {
        if (key.phase() == micropixel::KeyPhase::kDown && key.code() == micropixel::KeyCode::kConfirm) { Press(); }
    }

   private:
    void Press() {
        if (!spinning_) {
            if (credits_ < 1) { credits_ = 100; }
            --credits_;
            win_ = 0; stopped_ = 0; spinning_ = true;
            for (auto& reel : reels_) { reel.Start(); }
            portkit::Beep(app_, 440U, 60U);
        } else if (stopped_ < kReels) {
            reels_[stopped_++].Stop();
            portkit::Beep(app_, 300U + 120U * static_cast<uint32_t>(stopped_), 50U);
        }
    }

    void Finish() {
        spinning_ = false;
        const int32_t a = reels_[0].Center(), b = reels_[1].Center(), c = reels_[2].Center();
        if (a == b && b == c) { win_ = kPayout[a] * 4; }
        else if (a == b || b == c || a == c) { win_ = 2; }
        if (win_ > 0) {
            credits_ += win_;
            flash_ms_ = 600U;
            portkit::Beep(app_, win_ > 2 ? 988U : 660U, 240U);
        }
    }

    void Symbol(int32_t index, int32_t cx, int32_t cy) {
        switch (index) {
            case 0:  // seven
                canvas_.Fill(cx - 16, cy - 20, 32, 8, kRed);
                for (int32_t i = 0; i < 32; ++i) { canvas_.Fill(cx + 8 - i * 2 / 3, cy - 12 + i, 9, 1, kRed); }
                break;
            case 1:  // bar
                canvas_.Round(cx - 28, cy - 13, 56, 26, 5, Rgb(30, 30, 34));
                canvas_.Frame(cx - 28, cy - 13, 56, 26, kGold, 2);
                canvas_.Text(cx, cy - 8, "BAR", kGold, micropixel::SystemFont::kSmall, true);
                break;
            case 2:  // bell
                canvas_.Disc(cx, cy - 8, 13, kGold);
                canvas_.Fill(cx - 13, cy - 8, 26, 18, kGold);
                canvas_.Fill(cx - 18, cy + 10, 36, 5, kGold);
                canvas_.Disc(cx, cy + 18, 4, Rgb(190, 120, 30));
                break;
            case 3:  // cherries
                canvas_.Line(cx - 11, cy + 2, cx + 3, cy - 20, kGreen, 2);
                canvas_.Line(cx + 12, cy + 2, cx + 3, cy - 20, kGreen, 2);
                canvas_.Disc(cx - 12, cy + 9, 11, kRed);
                canvas_.Disc(cx + 12, cy + 9, 11, kRed);
                break;
            case 4:  // lemon
                canvas_.Disc(cx, cy, 16, Rgb(250, 226, 70));
                canvas_.Disc(cx - 16, cy, 4, Rgb(250, 226, 70));
                canvas_.Disc(cx + 16, cy, 4, Rgb(250, 226, 70));
                break;
            default:  // gem
                for (int32_t i = 0; i < 40; ++i) {
                    const int32_t half = i < 12 ? 10 + i * 2 : 34 - (i - 12) * 34 / 28;
                    canvas_.Fill(cx - half, cy - 18 + i, half * 2, 1, i < 12 ? Rgb(150, 230, 255) : Rgb(70, 170, 245));
                }
                break;
        }
    }

    void Draw() {
        canvas_.Begin();
        for (int32_t r = 0; r < kReels; ++r) {
            const int32_t x = 12 + r * (kReelW + 7);
            canvas_.Fill(x, kWindowTop, kReelW, kWindowBottom - kWindowTop, Rgb(245, 242, 232));
            const float offset = reels_[r].angle / kStep;
            const int32_t whole = static_cast<int32_t>(offset);
            const int32_t shift = static_cast<int32_t>((offset - static_cast<float>(whole)) * kSpacing);
            const int32_t middle = (kWindowTop + kWindowBottom) / 2;
            for (int32_t i = -2; i <= 2; ++i) {
                const int32_t y = middle + i * kSpacing + shift;
                if (y > kWindowTop - 34 && y < kWindowBottom + 34) {
                    Symbol(kSymbols[((i - whole) % kReelLength + kReelLength) % kReelLength], x + kReelW / 2, y);
                }
            }
        }
        // Panels drawn last hide symbols sliding past the window edges.
        canvas_.Fill(0, 0, canvas_.width(), kWindowTop, Rgb(26, 16, 46));
        canvas_.Fill(0, kWindowBottom, canvas_.width(), canvas_.height() - kWindowBottom, Rgb(26, 16, 46));
        const int32_t mid = (kWindowTop + kWindowBottom) / 2;
        canvas_.Fill(4, mid - 1, 6, 3, kRed);
        canvas_.Fill(canvas_.width() - 10, mid - 1, 6, 3, kRed);
        canvas_.Text(12, 9, "CREDITS", Rgb(170, 160, 200));
        canvas_.Number(88, 9, credits_, kInk);
        if (win_ > 0) { canvas_.Text(canvas_.width() - 12 - 90, 9, "WIN", kGold); canvas_.Number(canvas_.width() - 62, 9, win_, kGold); }
        const char* prompt = !spinning_ ? "TAP TO SPIN" : (stopped_ < kReels ? "TAP TO STOP" : "...");
        const Color color = flash_ms_ % 200U > 100U ? kGold : (spinning_ ? kRed : kGreen);
        portkit::Button(canvas_, {12, 196, canvas_.width() - 24, 38}, prompt, color, Rgb(20, 12, 30), micropixel::SystemFont::kMedium);
        canvas_.Present();
    }

    Application& app_;
    Canvas canvas_;
    Reel reels_[kReels]{};
    int32_t credits_{100}, win_{}, stopped_{};
    bool spinning_{};
    uint32_t flash_ms_{};
};

}  // namespace

int main() {
    Application app;
    SlotMachine game(app);
    return portkit::Run(app, game);
}
