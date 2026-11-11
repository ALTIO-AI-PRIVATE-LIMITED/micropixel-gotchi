// SPDX-License-Identifier: Apache-2.0
//
// Dice Cup: roll up to six dice with a tap or a shake.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kP[7][9] = {{0},{0,0,0,0,1,0,0,0,0},{1,0,0,0,0,0,0,0,1},{1,0,0,0,1,0,0,0,1},{1,0,1,0,0,0,1,0,1},{1,0,1,0,1,0,1,0,1},{1,0,1,1,0,1,1,0,1}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 60, 46)), t_(a) { for (auto& v : d_) { v = 1 + static_cast<int32_t>(a_.random().Below(6U)); } }
    void Tick(uint32_t) {
        t_.Poll(); if (cool_ > 0) { --cool_; }
        if (t_.jolt() && cool_ == 0 && roll_ == 0) { Roll(); }
        if (roll_ > 0) { --roll_; if (roll_ % 2 == 0) { for (int32_t i = 0; i < n_; ++i) { d_[i] = 1 + static_cast<int32_t>(a_.random().Below(6U)); } Beep(a_, 200U + a_.random().Below(300U), 16U, 90U, W::kNoise); } }
        Draw();
    }
    void Press(int32_t x, int32_t y) { if (y > 194) { if (x < 90) { n_ = n_ > 1 ? n_ - 1 : 1; } else if (x > 206) { n_ = n_ < 6 ? n_ + 1 : 6; } else { Roll(); } } else { Roll(); } }
    void Button(KeyCode k) { if (k == kBoot) { Roll(); } else if (k == kVolUp) { n_ = n_ < 6 ? n_ + 1 : 6; } else { n_ = n_ > 1 ? n_ - 1 : 1; } }

   private:
    void Roll() { if (roll_ == 0) { roll_ = 18; cool_ = 40; } }
    void Die(int32_t x, int32_t y, int32_t s, int32_t v) { c_.Round(x, y, s, s, s / 7, Rgb(245, 243, 232)); const int32_t g = s / 4; for (int32_t i = 0; i < 9; ++i) { if (kP[v][i] != 0) { c_.Disc(x + g + (i % 3) * g, y + g + (i / 3) * g, s / 11, Rgb(30, 30, 44)); } } }
    void Draw() {
        c_.Begin();
        const int32_t s = n_ <= 2 ? 92 : 62, cols = n_ <= 3 ? n_ : 3, rows = n_ <= 3 ? 1 : 2, gap = 12, tw = cols * s + (cols - 1) * gap, th = rows * s + (rows - 1) * gap, sum0 = 0; int32_t sum = sum0;
        for (int32_t i = 0; i < n_; ++i) { const int32_t rx = roll_ > 0 ? static_cast<int32_t>(a_.random().Below(7U)) - 3 : 0, x = (kW - tw) / 2 + (i % 3) * (s + gap) + rx, y = 4 + (186 - th) / 2 + (i / 3) * (s + gap); Die(x, y, s, d_[i]); sum += d_[i]; }
        c_.Round(6, 198, 80, 36, 8, Rgb(30, 100, 76)); c_.Text(46, 204, "-", kWhite, kMedium, true);
        c_.Round(210, 198, 80, 36, 8, Rgb(30, 100, 76)); c_.Text(250, 204, "+", kWhite, kMedium, true);
        char line[20] = "Total "; Canvas::Format(line + 6, sum); c_.Text(kW / 2, 206, roll_ > 0 ? "Rolling" : line, kWhite, kMedium, true);
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; int32_t d_[6]{}, n_{2}, roll_{}, cool_{};
};
}  // namespace
PORTKIT_MAIN(Game)
