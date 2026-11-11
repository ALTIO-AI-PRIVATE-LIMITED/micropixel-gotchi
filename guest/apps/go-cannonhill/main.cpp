// SPDX-License-Identifier: Apache-2.0
//
// Cannon Hill: pull back and let go to fire over the hills at the computer's cannon.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(150, 210, 245), 1000U, 12U) { New(); }
    void Tick(uint32_t) { ++f_; Step(); Draw(); }
    void Press(int32_t x, int32_t y) { if (s_ == 3) { New(); return; } if (s_ == 0) { aim_ = true; ax_ = x; ay_ = y; } }
    void Drag(int32_t x, int32_t y) { ax_ = x; ay_ = y; }
    void Tap(int32_t, int32_t) { if (s_ == 0 && aim_) { aim_ = false; Fire(); } }
    void Button(KeyCode k) { if (k == kBoot && s_ == 0) { ax_ = px_ - 30; ay_ = h_[px_] + 10; Fire(); } }

   private:
    void New() { float a = static_cast<float>(a_.random().Below(100U)) * 0.06F, b = static_cast<float>(a_.random().Below(100U)) * 0.06F; for (int32_t x = 0; x < kW; ++x) { h_[x] = 170 + static_cast<int32_t>(m::Sin(static_cast<float>(x) * 0.021F + a) * 26.0F + m::Sin(static_cast<float>(x) * 0.058F + b) * 12.0F); } px_ = 30; ex_ = 262; hp_[0] = hp_[1] = 3; s_ = 0; Wind(); msg_ = "Pull back and let go"; }
    void Wind() { wind_ = (static_cast<float>(a_.random().Below(41U)) - 20.0F) * 0.002F; }
    void Fire() { const float dx = static_cast<float>(px_ - ax_), dy = static_cast<float>(h_[px_] - 8 - ay_); Shoot(static_cast<float>(px_), static_cast<float>(h_[px_] - 14), m::Clamp(dx * 0.08F, -9.0F, 9.0F), m::Clamp(dy * 0.08F, -9.0F, 9.0F)); }
    void Shoot(float x, float y, float vx, float vy) { bx_ = x; by_ = y; vx_ = vx; vy_ = vy; s_ = s_ == 0 ? 1 : 2; Beep(a_, 200U, 90U, 280U, W::kNoise); }
    void Blast(int32_t cx) { for (int32_t x = m::Max(cx - 18, 0); x < m::Min(cx + 18, kW); ++x) { const int32_t d = x - cx; h_[x] = m::Min(h_[x] + (18 - d * d / 18) / 2, 236); } Beep(a_, 100U, 220U, 300U, W::kNoise); }
    void Hit(int32_t cx) { Blast(cx); for (int32_t who = 0; who < 2; ++who) { const int32_t tx = who == 0 ? px_ : ex_; if (m::Abs(tx - cx) < 20) { --hp_[who]; } } if (hp_[0] <= 0 || hp_[1] <= 0) { s_ = 3; msg_ = hp_[1] <= 0 ? "You win" : "Computer wins"; return; } if (s_ == 1) { s_ = 2; wait_ = 26; msg_ = "Computer aiming"; } else { s_ = 0; Wind(); msg_ = "Your shot"; } }
    void Step() {
        if (wait_ > 0) { if (--wait_ == 0) { const float dist = static_cast<float>(ex_ - px_), vx = -(dist / 87.0F) + (static_cast<float>(a_.random().Below(21U)) - 10.0F) * 0.1F; Shoot(static_cast<float>(ex_), static_cast<float>(h_[ex_] - 14), vx, -6.5F); s_ = 2; } return; }
        if (s_ != 1 && s_ != 2) { return; }
        vy_ += 0.15F; vx_ += wind_; bx_ += vx_; by_ += vy_; const auto x = static_cast<int32_t>(bx_);
        if (x < 0 || x >= kW) { if (s_ == 1) { s_ = 2; wait_ = 26; msg_ = "Computer aiming"; } else { s_ = 0; Wind(); msg_ = "Your shot"; } return; }
        if (by_ >= static_cast<float>(h_[x])) { Hit(x); }
    }
    void Cannon(int32_t x, Color k, int32_t dir) { const int32_t y = h_[x]; c_.Round(x - 12, y - 14, 24, 14, 3, k); c_.Line(x, y - 12, x + dir * 14, y - 22, k, 3); }
    void Draw() {
        c_.Begin();
        c_.Disc(240, 34, 18, Rgb(255, 230, 130));
        for (int32_t x = 0; x < kW; x += 2) { c_.Fill(x, h_[x], 2, kH - h_[x], Rgb(100, 170, 90)); c_.Fill(x, h_[x], 2, 4, Rgb(70, 130, 70)); }
        Cannon(px_, Rgb(70, 90, 220), 1); Cannon(ex_, Rgb(220, 70, 80), -1);
        if (aim_) { const int32_t y0 = h_[px_] - 14; c_.Line(px_, y0, ax_, ay_, kWhite, 2); }
        if (s_ == 1 || s_ == 2) { c_.Disc(static_cast<int32_t>(bx_), static_cast<int32_t>(by_), 4, Rgb(30, 30, 40)); }
        c_.Text(kW / 2, 8, msg_, Rgb(20, 40, 70), kSmall, true); for (int32_t i = 0; i < hp_[0]; ++i) { c_.Disc(14 + i * 14, 34, 5, Rgb(70, 90, 220)); } for (int32_t i = 0; i < hp_[1]; ++i) { c_.Disc(kW - 14 - i * 14, 34, 5, Rgb(220, 70, 80)); }
        c_.Text(kW / 2, 28, wind_ > 0.0F ? "wind >" : (wind_ < 0.0F ? "< wind" : "calm"), Rgb(40, 60, 90), kSmall, true);
        if (s_ == 3) { Banner(c_, msg_, "Tap for a new hill", Rgb(255, 170, 90)); }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t h_[kW]{}, px_{}, ex_{}, hp_[2]{}, s_{}, ax_{}, ay_{}, f_{}, wait_{}; float bx_{}, by_{}, vx_{}, vy_{}, wind_{}; bool aim_{}; const char* msg_{""};
};
}  // namespace
PORTKIT_MAIN(Game)
