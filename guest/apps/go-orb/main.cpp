// SPDX-License-Identifier: Apache-2.0
//
// Answer Orb: ask a question out loud, shake or tap, read the answer.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kA[] = {"Absolutely","Not a chance","Ask after lunch","The stars shrug","Go for it","Sleep on it","Yes, but quietly","No, and do not ask twice","Probably","Unlikely, friend","Trust your gut","Flip a coin","Today is the day","Wait one more week","Only if you share","Yes with snacks","Better not","Ask your best friend","The answer is near","All signs say yes","Try again tomorrow","A big maybe"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 10, 40)), t_(a) {}
    void Tick(uint32_t) { t_.Poll(); ++f_; if (t_.jolt() && spin_ == 0) { Ask(); } if (spin_ > 0 && --spin_ == 0) { Beep(a_, 660U, 160U, 200U, W::kTriangle); } Draw(); }
    void Press(int32_t, int32_t) { if (spin_ == 0) { Ask(); } }
    void Button(KeyCode k) { if (k == kBoot && spin_ == 0) { Ask(); } }

   private:
    void Ask() { spin_ = 45; ans_ = static_cast<int32_t>(a_.random().Below(sizeof(kA) / sizeof(kA[0]))); asked_ = true; }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 24; ++i) { c_.Fill((i * 83 + f_ / 3) % kW, (i * 47) % kH, 2, 2, kDim); }
        const int32_t dx = spin_ > 0 ? static_cast<int32_t>(a_.random().Below(9U)) - 4 : 0, dy = spin_ > 0 ? static_cast<int32_t>(a_.random().Below(9U)) - 4 : 0, cx = 148 + dx, cy = 112 + dy;
        c_.Disc(cx, cy, 96, Rgb(70, 30, 150)); c_.Disc(cx - 30, cy - 34, 34, Rgb(110, 60, 200), 120U);
        c_.Disc(cx, cy, 62, Rgb(14, 12, 34));
        if (spin_ == 0 && asked_) {
            const char* s = kA[ans_]; char a[20]{}, b[20]{}; int32_t len = 0; while (s[len] != '\0') { ++len; }
            int32_t cut = len; if (len > 13) { cut = len / 2; while (cut < len && s[cut] != ' ') { ++cut; } } 
            int32_t i = 0; for (; i < cut && i < 19; ++i) { a[i] = s[i]; } if (cut < len) { int32_t j = 0; for (i = cut + 1; i < len && j < 19; ++i) { b[j++] = s[i]; } }
            c_.Text(cx, b[0] == '\0' ? cy - 7 : cy - 18, a, Rgb(200, 240, 255), kSmall, true); if (b[0] != '\0') { c_.Text(cx, cy + 2, b, Rgb(200, 240, 255), kSmall, true); }
        } else if (spin_ > 0) { c_.Ring(cx, cy, 20 + f_ % 34, Rgb(100, 180, 255)); } else { c_.Text(cx, cy - 7, "Ask me", kGrey, kSmall, true); }
        c_.Text(kW / 2, 216, "Shake or tap", kGrey, kSmall, true);
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; int32_t f_{}, spin_{}, ans_{}; bool asked_{};
};
}  // namespace
PORTKIT_MAIN(Game)
