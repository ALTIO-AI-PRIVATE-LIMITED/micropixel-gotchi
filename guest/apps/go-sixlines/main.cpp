// SPDX-License-Identifier: Apache-2.0
//
// Six Lines: toss three coins six times to build a figure, then read its two trigrams. A simplified reading for fun.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kT[8] = {"Earth","Thunder","Water","Lake","Mountain","Fire","Wind","Heaven"};
const char* const kM[8] = {"Be open and steady","Something stirs, act","Careful, deep water","Joy, share it","Stop and be still","See clearly","Bend and get through","Be strong, begin"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(30, 22, 18)), t_(a) {}
    void Tick(uint32_t) { t_.Poll(); if (t_.jolt() && cool_ == 0 && spin_ == 0) { Toss(); } if (cool_ > 0) { --cool_; } if (spin_ > 0 && --spin_ == 0) { Land(); } Draw(); }
    void Press(int32_t, int32_t) { if (spin_ == 0) { if (n_ >= 6) { n_ = 0; } else { Toss(); } } }
    void Button(KeyCode k) { if (k == kBoot) { Press(0, 0); } }

   private:
    void Toss() { spin_ = 22; cool_ = 40; for (auto& c : coin_) { c = a_.random().Below(2U) != 0U; } Beep(a_, 500U, 40U, 120U, W::kNoise); }
    void Land() { int32_t s = 0; for (bool c : coin_) { s += c ? 3 : 2; } line_[n_++] = s; Beep(a_, 700U, 50U); }
    int32_t Tri(int32_t from) const { int32_t v = 0; for (int32_t i = 0; i < 3; ++i) { if (line_[from + i] % 2 != 0) { v |= 1 << (2 - i); } } return v; }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 3; ++i) { const int32_t x = 200 + i * 32, y = 34; const bool on = spin_ > 0 ? (spin_ + i) / 2 % 2 == 0 : coin_[i]; c_.Disc(x, y, 13, on ? Rgb(230, 190, 90) : Rgb(170, 130, 60)); c_.Fill(x - 4, y - 4, 8, 8, Rgb(30, 22, 18)); }
        for (int32_t i = 0; i < 6; ++i) { const int32_t y = 200 - i * 28; if (i < n_) { const bool yang = line_[i] % 2 != 0, moving = line_[i] == 6 || line_[i] == 9; const Color k = moving ? Rgb(255, 170, 90) : Rgb(245, 234, 210); if (yang) { c_.Fill(24, y, 140, 12, k); } else { c_.Fill(24, y, 60, 12, k); c_.Fill(104, y, 60, 12, k); } } else { c_.Frame(24, y, 140, 12, kDim); } }
        if (n_ < 6) { c_.Text(232, 70, "Tap or shake", kGrey, kSmall, true); c_.Number(232, 96, 6 - n_, kWhite, kLarge, true); c_.Text(232, 130, "tosses left", kGrey, kSmall, true); }
        else { const int32_t lo = Tri(0), hi = Tri(3); c_.Text(182, 72, "Above", kGrey); c_.Text(182, 90, kT[hi], Rgb(255, 214, 110), kMedium); c_.Text(182, 112, kM[hi], kWhite); c_.Text(182, 142, "Below", kGrey); c_.Text(182, 160, kT[lo], Rgb(255, 214, 110), kMedium); c_.Text(182, 182, kM[lo], kWhite); c_.Text(182, 212, "Tap to begin again", kDim); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; bool coin_[3]{}; int32_t line_[6]{}, n_{}, spin_{}, cool_{};
};
}  // namespace
PORTKIT_MAIN(Game)
