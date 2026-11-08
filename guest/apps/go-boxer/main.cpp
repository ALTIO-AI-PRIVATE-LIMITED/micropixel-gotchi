// SPDX-License-Identifier: Apache-2.0
//
// Shake Boxer: flick the Gotchi or tap to punch when the guard drops. Tilt to dodge incoming hits.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(40, 20, 30)), t_(a), p_(a) { best_ = p_.Get("best"); }
    void Tick(uint32_t ms) { t_.Poll(); if (state_ == 1) { Step(static_cast<int32_t>(ms)); if (t_.jolt()) { Punch(); } } if (cool_ > 0) { --cool_; } if (hit_ > 0) { --hit_; } if (ow_ > 0) { --ow_; } Draw(); }
    void Press(int32_t, int32_t) { if (state_ != 1) { Start(); } else { Punch(); } }
    void Button(KeyCode k) { if (state_ != 1) { Start(); } else if (k == kBoot) { Punch(); } else { dodgeKey_ = k == kVolUp ? 1 : -1; dodgeT_ = 16; } }

   private:
    void Start() { hp_ = 100; me_ = 100; round_ = 1; score_ = 0U; state_ = 1; Next(); }
    void Next() { phase_ = static_cast<int32_t>(a_.random().Below(3U)); wait_ = 700 + static_cast<int32_t>(a_.random().Below(1100U)) - m::Min(round_ * 40, 400); side_ = a_.random().Below(2U) == 0U ? -1 : 1; }
    int32_t Dodge() const { if (dodgeT_ > 0) { return dodgeKey_; } return t_.x() > 0.3F ? 1 : (t_.x() < -0.3F ? -1 : 0); }
    void Step(int32_t ms) {
        wait_ -= ms; if (dodgeT_ > 0) { --dodgeT_; }
        if (wait_ > 0) { return; }
        if (phase_ == 1) { if (Dodge() == side_) { score_ += 15U; Beep(a_, 900U, 40U); } else { me_ -= 12; ow_ = 12; Beep(a_, 130U, 140U, 260U, W::kSquare); if (me_ <= 0) { End(); return; } } }
        Next();
    }
    void Punch() {
        if (cool_ > 0) { return; } cool_ = 9; hit_ = 7;
        if (phase_ == 0) { hp_ -= 9; score_ += 25U; Beep(a_, 200U, 60U, 340U, W::kNoise); if (hp_ <= 0) { ++round_; hp_ = 100; me_ = m::Min(me_ + 20, 100); score_ += 100U; Beep(a_, 880U, 250U); } Next(); }
        else { score_ = score_ > 2U ? score_ - 2U : 0U; Beep(a_, 400U, 30U, 90U); }
    }
    void End() { state_ = 2; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } }
    void Glove(int32_t x, int32_t y, Color k) { c_.Disc(x, y, 34, k); c_.Disc(x - 14, y - 16, 12, k.Lightened(40U)); }
    void Draw() {
        c_.Begin();
        c_.Fill(0, 150, kW, 90, Rgb(90, 50, 40)); c_.Fill(0, 60, kW, 3, Rgb(230, 230, 230)); c_.Fill(0, 96, kW, 3, Rgb(200, 60, 60));
        const int32_t x = 148 + (hit_ > 0 ? hit_ * 2 : 0);
        c_.Round(x - 44, 150, 88, 70, 14, Rgb(90, 200, 130));
        c_.Disc(x, 112, 44, Rgb(240, 196, 140)); c_.Disc(x - 15, 104, 6, Rgb(30, 30, 40)); c_.Disc(x + 15, 104, 6, Rgb(30, 30, 40)); c_.Fill(x - 13, 136, 26, 5, Rgb(120, 50, 50));
        if (phase_ == 0) { Glove(x - 96, 182, Rgb(220, 60, 60)); Glove(x + 96, 182, Rgb(220, 60, 60)); c_.Text(kW / 2, 56, "OPEN", Rgb(140, 250, 160), kMedium, true); }
        else if (phase_ == 1) { const int32_t gx = side_ < 0 ? 40 : 256, near = m::Clamp(40 - wait_ / 30, 0, 40); c_.Disc(gx, 120, 30 + near, Rgb(230, 70, 70)); c_.Text(kW / 2, 56, side_ < 0 ? "<< LEAN" : "LEAN >>", Rgb(255, 210, 110), kMedium, true); }
        else { Glove(x - 28, 112, Rgb(220, 60, 60)); Glove(x + 28, 112, Rgb(220, 60, 60)); c_.Text(kW / 2, 56, "GUARD", kGrey, kMedium, true); }
        if (hit_ > 0) { Glove(148, 210, Rgb(70, 130, 255)); }
        c_.Fill(8, 8, 120, 10, kDim); c_.Fill(8, 8, m::Max(me_, 0) * 120 / 100, 10, Rgb(90, 220, 130)); c_.Fill(168, 8, 120, 10, kDim); c_.Fill(168, 8, hp_ * 120 / 100, 10, Rgb(255, 110, 90));
        c_.Number(kW / 2, 26, static_cast<int32_t>(score_), kWhite, kMedium, true); c_.Text(8, 22, "YOU", kGrey); c_.Text(254, 22, "RING", kGrey); c_.Number(288, 22, round_, kWhite);
        if (ow_ > 0) { c_.Fill(0, 0, kW, kH, Rgb(255, 40, 40), 120U); }
        if (state_ == 0) { Banner(c_, "SHAKE BOXER", "Tap to start", Rgb(255, 120, 90)); } if (state_ == 2) { Banner(c_, "KNOCKED OUT", "Tap for a rematch", Rgb(255, 120, 90)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Prefs p_; int32_t hp_{100}, me_{100}, round_{1}, state_{}, phase_{}, wait_{}, side_{1}, cool_{}, hit_{}, ow_{}, dodgeT_{}, dodgeKey_{}; uint32_t score_{}, best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
