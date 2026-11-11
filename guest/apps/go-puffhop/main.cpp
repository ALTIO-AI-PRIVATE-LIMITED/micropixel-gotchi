// SPDX-License-Identifier: Apache-2.0
//
// Puff Hop: tap to flutter a puffball through the gaps.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(120, 200, 235)), p_(a) { best_ = p_.Get("best"); Reset(); }
    void Tick(uint32_t) { ++f_; if (state_ == 1) { Step(); } Draw(); }
    void Press(int32_t, int32_t) { Act(); }
    void Button(KeyCode k) { if (k == kBoot) { Act(); } }

   private:
    void Reset() { y_ = 110.0F; v_ = 0.0F; score_ = 0U; for (int32_t i = 0; i < 3; ++i) { x_[i] = 300.0F + static_cast<float>(i) * 120.0F; gap_[i] = Gap(); passed_[i] = false; } }
    int32_t Gap() { return 60 + static_cast<int32_t>(a_.random().Below(110U)); }
    void Act() { if (state_ == 1) { v_ = -5.2F; Beep(a_, 620U, 30U, 120U); } else if (state_ == 0 || hold_ > 18) { Reset(); state_ = 1; v_ = -5.2F; } }
    void Step() {
        v_ += 0.34F; y_ += v_; const float speed = 2.2F + static_cast<float>(score_) * 0.02F;
        bool hit = y_ < 8.0F || y_ > 210.0F;
        for (int32_t i = 0; i < 3; ++i) { x_[i] -= speed; if (x_[i] < -36.0F) { x_[i] += 360.0F; gap_[i] = Gap(); passed_[i] = false; } if (!passed_[i] && x_[i] + 34.0F < 60.0F) { passed_[i] = true; ++score_; Beep(a_, 880U, 40U); } if (x_[i] < 80.0F && x_[i] + 34.0F > 42.0F && (y_ < static_cast<float>(gap_[i]) - 36.0F || y_ > static_cast<float>(gap_[i]) + 36.0F)) { hit = true; } }
        if (hit) { state_ = 2; hold_ = 0; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } Beep(a_, 160U, 200U); }
    }
    void Draw() {
        if (state_ == 2) { ++hold_; }
        c_.Begin();
        c_.Fill(0, 214, kW, 26, Rgb(120, 170, 90));
        for (int32_t i = 0; i < 3; ++i) { const auto x = static_cast<int32_t>(x_[i]); c_.Fill(x, 0, 34, gap_[i] - 44, Rgb(210, 110, 150)); c_.Fill(x, gap_[i] + 44, 34, 214 - gap_[i] - 44, Rgb(210, 110, 150)); c_.Fill(x - 4, gap_[i] - 56, 42, 12, Rgb(235, 140, 175)); c_.Fill(x - 4, gap_[i] + 44, 42, 12, Rgb(235, 140, 175)); }
        const auto y = static_cast<int32_t>(y_); c_.Disc(60, y, 15, Rgb(255, 235, 170)); c_.Disc(66, y - 4, 4, Rgb(40, 40, 60)); for (int32_t i = 0; i < 3; ++i) { c_.Disc(46 - i, y - 8 + i * 8 + (f_ / 3) % 3, 3, Rgb(255, 255, 240)); }
        c_.Number(kW / 2, 10, static_cast<int32_t>(score_), kWhite, kTitle, true);
        if (state_ == 0) { Banner(c_, "PUFF HOP", "Tap to flutter", Rgb(255, 170, 200)); } if (state_ == 2) { Banner(c_, "BONK", "Tap to go again", Rgb(255, 170, 200)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; float y_{}, v_{}, x_[3]{}; int32_t gap_[3]{}, state_{}, hold_{}, f_{}; bool passed_[3]{}; uint32_t score_{}, best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
