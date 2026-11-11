// SPDX-License-Identifier: Apache-2.0
//
// Brick Bash: bounce a ball through walls of bricks. Drag, tilt or use the volume keys to move the paddle.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kBC = 11, kBR = 6, kBW = 26, kBH = 11, kPY = 216;
constexpr Color kRows[] = {Rgb(255,100,110),Rgb(255,160,80),Rgb(255,220,90),Rgb(130,230,120),Rgb(100,190,255),Rgb(180,130,255)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(12, 14, 28)), t_(a), p_(a) { best_ = p_.Get("best"); Level(); }
    void Tick(uint32_t) { t_.Poll(); if (state_ == 1) { Step(); } else { ballx_ = pad_; bally_ = static_cast<float>(kPY - 7); } Draw(); }
    void Press(int32_t x, int32_t) { touch_ = x; pad_ = static_cast<float>(x); }
    void Drag(int32_t x, int32_t) { touch_ = x; pad_ = static_cast<float>(x); }
    void Tap(int32_t, int32_t) { touch_ = -1; Serve(); }
    void Button(KeyCode k) { if (k == kBoot) { Serve(); } else { pad_ += k == kVolUp ? 26.0F : -26.0F; touch_ = 1; } }

   private:
    void Serve() { if (state_ == 3) { lives_ = 3; score_ = 0U; level_ = 1; Level(); state_ = 0; return; } if (state_ == 0) { state_ = 1; vx_ = 2.4F; vy_ = -3.4F - static_cast<float>(level_) * 0.2F; } }
    void Level() { left_ = 0; for (int32_t y = 0; y < kBR; ++y) { for (int32_t x = 0; x < kBC; ++x) { const bool on = level_ % 3 == 1 || (level_ % 3 == 2 ? (x + y) % 2 == 0 : (x % 4 != 3)); b_[y][x] = on; left_ += on ? 1 : 0; } } }
    void Step() {
        if (touch_ < 0) { pad_ += t_.x() * 7.0F; }
        pad_ = m::Clamp(pad_, 26.0F, 270.0F);
        ballx_ += vx_; if (ballx_ < 5.0F || ballx_ > 291.0F) { vx_ = -vx_; ballx_ += vx_; Beep(a_, 300U, 15U, 90U); }
        bally_ += vy_; if (bally_ < 26.0F) { vy_ = -vy_; bally_ += vy_; }
        const auto bx = static_cast<int32_t>(ballx_ - 5.0F) / kBW, by = (static_cast<int32_t>(bally_) - 34) / kBH;
        if (bally_ >= 34.0F && by >= 0 && by < kBR && bx >= 0 && bx < kBC && b_[by][bx]) { b_[by][bx] = false; --left_; vy_ = -vy_; score_ += 10U; Beep(a_, 500U + static_cast<uint32_t>(by) * 60U, 30U); if (left_ == 0) { ++level_; Level(); state_ = 0; Beep(a_, 980U, 200U); } }
        if (vy_ > 0.0F && bally_ >= static_cast<float>(kPY - 5) && bally_ < static_cast<float>(kPY + 8) && m::Abs(ballx_ - pad_) < 30.0F) { vy_ = -vy_; vx_ = (ballx_ - pad_) * 0.14F; Beep(a_, 420U, 20U, 140U); }
        if (bally_ > 244.0F) { --lives_; Beep(a_, 150U, 220U); if (lives_ <= 0) { state_ = 3; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } } else { state_ = 0; } }
    }
    void Draw() {
        c_.Begin();
        for (int32_t y = 0; y < kBR; ++y) { for (int32_t x = 0; x < kBC; ++x) { if (b_[y][x]) { c_.Fill(5 + x * kBW, 34 + y * kBH, kBW - 2, kBH - 2, kRows[y]); } } }
        c_.Round(static_cast<int32_t>(pad_) - 28, kPY, 56, 9, 4, Rgb(235, 240, 255));
        c_.Disc(static_cast<int32_t>(ballx_), static_cast<int32_t>(bally_), 5, Rgb(255, 230, 140));
        c_.Number(8, 5, static_cast<int32_t>(score_), kWhite, kMedium);
        for (int32_t i = 0; i < lives_; ++i) { c_.Disc(kW - 14 - i * 16, 14, 5, Rgb(255, 110, 130)); }
        c_.Number(kW / 2, 5, level_, kGrey, kMedium, true);
        if (state_ == 3) { Banner(c_, "ALL OUT", "Tap to play again", Rgb(255, 110, 130)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Prefs p_; bool b_[kBR][kBC]{}; float pad_{148.0F}, ballx_{148.0F}, bally_{200.0F}, vx_{}, vy_{}; int32_t state_{}, lives_{3}, level_{1}, left_{}, touch_{-1}; uint32_t score_{}, best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
