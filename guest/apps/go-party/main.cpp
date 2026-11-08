// SPDX-License-Identifier: Apache-2.0
//
// Party Pack: three quick group games on one Gotchi. BOOT goes back to the menu.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
const Box kItem[3] = {{24, 62, 248, 48}, {24, 118, 248, 48}, {24, 174, 248, 48}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(30, 16, 52)) {}
    void Tick(uint32_t ms) { t_ += ms; Step(ms); Draw(); }
    void Press(int32_t x, int32_t y) {
        if (mode_ == 0) { for (int32_t i = 0; i < 3; ++i) { if (kItem[i].Has(x, y)) { Open(i + 1); } } return; }
        if (mode_ == 1) { Bomb(); return; }
        const int32_t side = x < kW / 2 ? 0 : 1;
        if (mode_ == 2) { Duel(side); return; }
        if (mode_ == 3 && phase_ == 1) { ++taps_[side]; Beep(a_, side == 0 ? 500U : 760U, 20U, 90U); return; }
        if (phase_ == 2 && hold_ > 700) { Open(mode_); }
    }
    void Button(KeyCode k) { if (k == kBoot) { mode_ = 0; } }

   private:
    void Open(int32_t m) { mode_ = m; phase_ = 0; hold_ = 0; taps_[0] = taps_[1] = 0; winner_ = -1; fuse_ = 6000 + static_cast<int32_t>(a_.random().Below(14000U)); wait_ = 1500 + static_cast<int32_t>(a_.random().Below(4000U)); passes_ = 0; if (m == 3) { phase_ = 1; wait_ = 10000; } }
    void Bomb() { if (phase_ == 2) { if (hold_ > 700) { Open(1); } return; } phase_ = 1; ++passes_; Beep(a_, 880U, 40U); }
    void Duel(int32_t side) {
        if (phase_ == 2) { if (hold_ > 700) { Open(2); } return; }
        if (wait_ > 0) { winner_ = 1 - side; early_ = true; } else { winner_ = side; early_ = false; }
        phase_ = 2; hold_ = 0; Beep(a_, 660U, 160U);
    }
    void Step(uint32_t ms) {
        const auto d = static_cast<int32_t>(ms); hold_ += d;
        if (mode_ == 1 && phase_ == 1) { fuse_ -= d; if ((t_ / (fuse_ < 3000 ? 160U : 420U)) != ((t_ - ms) / (fuse_ < 3000 ? 160U : 420U))) { Beep(a_, 1100U, 18U, 110U); } if (fuse_ <= 0) { phase_ = 2; hold_ = 0; Beep(a_, 70U, 700U, 320U, micropixel::Waveform::kNoise); } }
        if (mode_ == 2 && phase_ == 0) { const bool was = wait_ > 0; wait_ -= d; if (was && wait_ <= 0) { Beep(a_, 1040U, 120U); } }
        if (mode_ == 3 && phase_ == 1) { wait_ -= d; if (wait_ <= 0) { phase_ = 2; hold_ = 0; winner_ = taps_[0] == taps_[1] ? -1 : (taps_[0] > taps_[1] ? 0 : 1); Beep(a_, 660U, 200U); } }
    }
    void Draw() {
        c_.Begin();
        if (mode_ == 0) {
            c_.Text(kW / 2, 16, "PARTY PACK", Rgb(255, 214, 90), kLarge, true);
            const char* const names[] = {"Hot Bomb", "Quick Draw", "Tap Race"};
            const char* const notes[] = {"pass it before it blows", "tap first when it goes green", "10 seconds, left vs right"};
            const Color cols[] = {Rgb(255, 96, 90), Rgb(90, 210, 130), Rgb(90, 170, 255)};
            for (int32_t i = 0; i < 3; ++i) { const Box& b = kItem[i]; c_.Round(b.x, b.y, b.w, b.h, 10, cols[i].Darkened(90U)); c_.Text(b.x + 14, b.y + 5, names[i], kWhite, kMedium); c_.Text(b.x + 14, b.y + 27, notes[i], kWhite); }
        } else if (mode_ == 1) { DrawBomb(); } else { DrawVs(); }
        c_.Present();
    }
    void DrawBomb() {
        if (phase_ == 2) { c_.Fill(0, 0, kW, kH, (t_ / 90U) % 2U == 0U ? Rgb(255, 120, 40) : Rgb(170, 30, 30)); c_.Text(kW / 2, 70, "BOOM", kWhite, kTitle, true); c_.Text(kW / 2, 122, "Whoever holds it is out", kWhite, kSmall, true); c_.Text(kW / 2, 150, "Tap to play again", kWhite, kSmall, true); return; }
        const int32_t wob = phase_ == 1 && fuse_ < 3000 ? static_cast<int32_t>(t_ / 40U) % 5 - 2 : 0;
        c_.Disc(148 + wob, 134, 56, Rgb(44, 44, 56)); c_.Disc(128 + wob, 112, 11, Rgb(120, 120, 140));
        c_.Line(180 + wob, 86, 212, 54, Rgb(190, 160, 110), 4);
        if (phase_ == 1) { c_.Disc(214, 50, 6 + static_cast<int32_t>(t_ / 60U) % 5, Rgb(255, 180, 60)); }
        c_.Text(kW / 2, 14, phase_ == 0 ? "Tap to light the fuse" : "Tap and pass it on", kWhite, kMedium, true);
        c_.Number(kW / 2, 202, passes_, kGrey, kMedium, true);
    }
    void DrawVs() {
        const Color go = Rgb(70, 200, 120), wait = Rgb(150, 60, 70);
        const Color bg = mode_ == 2 && phase_ == 0 ? (wait_ > 0 ? wait : go) : Rgb(50, 40, 90);
        c_.Fill(0, 0, kW / 2 - 1, kH, winner_ == 0 ? go : bg); c_.Fill(kW / 2 + 1, 0, kW / 2, kH, winner_ == 1 ? go : bg);
        if (mode_ == 3) { c_.Number(74, 90, taps_[0], kWhite, kTitle, true); c_.Number(222, 90, taps_[1], kWhite, kTitle, true); if (phase_ == 1) { c_.Number(kW / 2, 8, wait_ / 1000 + 1, kWhite, kMedium, true); } }
        else if (phase_ == 0) { c_.Text(kW / 2, 100, wait_ > 0 ? "WAIT" : "GO", kWhite, kTitle, true); }
        if (phase_ == 2) {
            c_.Text(kW / 2, 40, winner_ < 0 ? "DRAW" : (winner_ == 0 ? "LEFT WINS" : "RIGHT WINS"), kWhite, kLarge, true);
            if (mode_ == 2 && early_) { c_.Text(kW / 2, 72, "Someone jumped early", kWhite, kSmall, true); }
            c_.Text(kW / 2, 190, "Tap to play again", kWhite, kSmall, true);
        }
    }
    Application& a_; Canvas c_;
    uint32_t t_{}; int32_t mode_{}, phase_{}, hold_{}, taps_[2]{}, winner_{-1}, fuse_{}, wait_{}, passes_{}; bool early_{};
};
}  // namespace
PORTKIT_MAIN(Game)
