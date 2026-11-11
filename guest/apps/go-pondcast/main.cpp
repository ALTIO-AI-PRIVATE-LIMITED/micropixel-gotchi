// SPDX-License-Identifier: Apache-2.0
//
// Pond Cast: tap to cast, tap when the float dips, then hold to reel without snapping the line.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct F { const char* name; int32_t pull, size; }; const F kF[] = {{"Minnow",1,2},{"Perch",2,4},{"Trout",3,7},{"Carp",4,12},{"Pike",6,18},{"Old Whisker",8,30}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(140, 210, 240)), p_(a) { best_ = p_.Get("best"); total_ = p_.Get("count"); }
    void Tick(uint32_t ms) { ++f_; Step(static_cast<int32_t>(ms)); Draw(); }
    void Press(int32_t, int32_t) { Down(); }
    void Tap(int32_t, int32_t) { held_ = false; }
    void Button(KeyCode k) { if (k == kBoot) { Down(); held_ = false; } }

   private:
    void Down() { held_ = true; if (s_ == 0) { s_ = 1; pow_ = 0; } else if (s_ == 1) { s_ = 2; wait_ = 1500 + static_cast<int32_t>(a_.random().Below(5000U)); dist_ = 40 + pow_ * 2; held_ = false; } else if (s_ == 2) { s_ = 0; msg_ = "Too early"; } else if (s_ == 3) { Hook(); } else if (s_ == 5) { s_ = 0; msg_ = "Tap to cast"; } }
    void Hook() { uint32_t r = a_.random().Below(100U); kind_ = r < 30U ? 0 : (r < 58U ? 1 : (r < 78U ? 2 : (r < 91U ? 3 : (r < 98U ? 4 : 5)))); s_ = 4; ten_ = 40; left_ = 100; Beep(a_, 700U, 60U); }
    void Step(int32_t ms) {
        if (s_ == 1) { pow_ = (pow_ + 3) % 200; }
        if (s_ == 2) { wait_ -= ms; if (wait_ <= 0) { s_ = 3; wait_ = 800; Beep(a_, 1040U, 80U); } }
        else if (s_ == 3) { wait_ -= ms; if (wait_ <= 0) { s_ = 0; msg_ = "It got away"; } }
        else if (s_ == 4) {
            const int32_t pull = kF[kind_].pull;
            if (held_) { ten_ += 1 + pull / 2 + ((f_ / 12) % 2) * pull; left_ -= 1; } else { ten_ -= 3; left_ += pull / 3; }
            ten_ = m::Clamp(ten_, 0, 110); left_ = m::Min(left_, 120);
            if (ten_ >= 100) { s_ = 0; msg_ = "Line snapped"; Beep(a_, 150U, 200U); }
            else if (ten_ <= 0 && !held_) { s_ = 0; msg_ = "It slipped off"; }
            else if (left_ <= 0) { s_ = 5; ++total_; p_.Set("count", total_); if (kF[kind_].size > static_cast<int32_t>(best_)) { best_ = static_cast<uint32_t>(kF[kind_].size); p_.Set("best", best_); } Beep(a_, 880U, 260U); }
        }
    }
    void Draw() {
        c_.Begin(); c_.Fill(0, 100, kW, 140, Rgb(50, 120, 170)); c_.Fill(0, 96, kW, 6, Rgb(120, 190, 120));
        for (int32_t i = 0; i < 9; ++i) { c_.Fill((i * 41 + f_ / 2) % kW, 120 + i * 13, 26, 2, Rgb(90, 160, 210)); }
        c_.Fill(246, 36, 44, 64, Rgb(120, 90, 60)); c_.Line(252, 44, 170, 26, Rgb(90, 60, 40), 3);
        const int32_t fx = 170 - dist_ / 2 > 20 ? 170 - dist_ / 2 : 20, dip = s_ == 3 ? 10 : 0, wob = (f_ / 8) % 2;
        if (s_ >= 2 && s_ <= 4) { c_.Line(170, 26, fx, 114 + dip, kWhite, 1); c_.Disc(fx, 114 + dip + wob, 5, Rgb(255, 70, 70)); }
        c_.Text(10, 6, "Caught", Rgb(30, 50, 70)); c_.Number(66, 6, static_cast<int32_t>(total_), Rgb(30, 50, 70)); c_.Text(10, 26, "Biggest", Rgb(30, 50, 70)); c_.Number(66, 26, static_cast<int32_t>(best_), Rgb(30, 50, 70));
        if (s_ == 1) { c_.Fill(30, 60, 200, 14, Rgb(30, 40, 60)); c_.Fill(30, 60, pow_, 14, Rgb(255, 180, 70)); c_.Text(130, 78, "Tap for distance", Rgb(30, 50, 70), kSmall, true); }
        if (s_ == 0) { c_.Text(kW / 2, 160, msg_, kWhite, kMedium, true); }
        if (s_ == 2) { c_.Text(kW / 2, 160, "Wait for the dip", kWhite, kSmall, true); } if (s_ == 3) { c_.Text(kW / 2, 160, "NOW", kWhite, kLarge, true); }
        if (s_ == 4) { c_.Text(30, 150, "Tension", kWhite); c_.Fill(30, 170, 236, 14, Rgb(20, 40, 70)); c_.Fill(30, 170, m::Min(ten_, 100) * 236 / 100, 14, ten_ > 75 ? Rgb(255, 80, 80) : Rgb(120, 230, 150)); c_.Text(30, 192, "Distance", kWhite); c_.Fill(30, 212, 236, 10, Rgb(20, 40, 70)); c_.Fill(30, 212, m::Max(left_, 0) * 236 / 120, 10, Rgb(255, 214, 110)); }
        if (s_ == 5) { Banner(c_, kF[kind_].name, "A fine catch. Tap to cast again", Rgb(120, 230, 150)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; int32_t f_{}, s_{}, pow_{}, wait_{}, dist_{}, kind_{}, ten_{}, left_{}; uint32_t best_{}, total_{}; bool held_{}; const char* msg_{"Tap to cast"};
};
}  // namespace
PORTKIT_MAIN(Game)
