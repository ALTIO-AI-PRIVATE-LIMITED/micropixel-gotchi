// SPDX-License-Identifier: Apache-2.0
//
// ABC Buddy: learn letters with pictures, then play find the letter.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct L { const char* word; uint8_t pic; Color col; };
const L kL[26] = {{"Apple",0,Rgb(230,60,70)},{"Ball",0,Rgb(70,130,240)},{"Cat",3,Rgb(240,170,70)},{"Duck",4,Rgb(250,210,70)},{"Egg",1,Rgb(245,240,220)},{"Fish",5,Rgb(90,190,230)},{"Grapes",6,Rgb(140,70,190)},{"House",7,Rgb(220,110,80)},{"Ice",2,Rgb(170,220,250)},{"Jam",8,Rgb(200,50,110)},{"Kite",9,Rgb(240,90,130)},{"Leaf",1,Rgb(100,200,90)},{"Moon",0,Rgb(240,230,170)},{"Nest",8,Rgb(150,110,70)},{"Orange",0,Rgb(250,150,50)},{"Pig",3,Rgb(250,170,190)},{"Queen",10,Rgb(250,210,90)},{"Rain",11,Rgb(100,160,240)},{"Sun",12,Rgb(255,210,70)},{"Tree",13,Rgb(70,170,90)},{"Up",14,Rgb(120,200,120)},{"Van",15,Rgb(90,150,220)},{"Water",11,Rgb(70,170,240)},{"Box",2,Rgb(190,140,80)},{"Yarn",0,Rgb(220,110,200)},{"Zebra",3,Rgb(235,235,235)}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(252, 244, 220)) {}
    void Tick(uint32_t) { if (fb_ > 0 && --fb_ == 0 && right_) { Ask(); } Draw(); }
    void Press(int32_t x, int32_t y) { if (y < 36 && x > 200) { quiz_ = !quiz_; if (quiz_) { Ask(); } return; } if (quiz_) { if (y > 130) { Choose(m::Clamp(x / 98, 0, 2)); } } else { i_ = (i_ + (x < 148 ? 25 : 1)) % 26; Beep(a_, 400U + static_cast<uint32_t>(i_) * 24U, 120U, 200U, W::kTriangle); } }
    void Button(KeyCode k) { if (k == kBoot) { quiz_ = !quiz_; if (quiz_) { Ask(); } } else if (!quiz_) { i_ = (i_ + (k == kVolUp ? 1 : 25)) % 26; } }

   private:
    void Ask() { i_ = static_cast<int32_t>(a_.random().Below(26U)); slot_ = static_cast<int32_t>(a_.random().Below(3U)); for (int32_t s = 0; s < 3; ++s) { if (s == slot_) { c_i_[s] = i_; continue; } bool dup; do { c_i_[s] = static_cast<int32_t>(a_.random().Below(26U)); dup = c_i_[s] == i_; for (int32_t q = 0; q < s; ++q) { dup = dup || c_i_[s] == c_i_[q]; } } while (dup); } right_ = false; fb_ = 0; }
    void Choose(int32_t s) { if (fb_ > 0) { return; } right_ = s == slot_; fb_ = right_ ? 30 : 16; if (right_) { ++score_; Beep(a_, 880U, 140U); } else { Beep(a_, 200U, 140U); } }
    void Pic(uint8_t p, int32_t cx, int32_t cy, Color k) {
        const Color dark = k.Darkened(90U);
        switch (p) {
            case 0: c_.Disc(cx, cy, 34, k); c_.Disc(cx - 12, cy - 12, 8, k.Lightened(120U)); break;
            case 1: for (int32_t i = -34; i <= 34; ++i) { const int32_t w = 30 - i * i / 40; c_.Fill(cx - w, cy + i, 2 * w, 1, k); } break;
            case 2: c_.Round(cx - 30, cy - 30, 60, 60, 6, k); c_.Frame(cx - 30, cy - 30, 60, 60, dark, 2); break;
            case 3: c_.Disc(cx, cy + 6, 30, k); c_.Fill(cx - 26, cy - 34, 14, 22, k); c_.Fill(cx + 12, cy - 34, 14, 22, k); c_.Disc(cx - 10, cy, 4, Rgb(30, 30, 40)); c_.Disc(cx + 10, cy, 4, Rgb(30, 30, 40)); c_.Fill(cx - 4, cy + 10, 8, 4, dark); break;
            case 4: c_.Disc(cx - 4, cy + 10, 28, k); c_.Disc(cx + 18, cy - 18, 16, k); c_.Fill(cx + 32, cy - 20, 14, 8, Rgb(250, 140, 40)); c_.Disc(cx + 22, cy - 22, 3, Rgb(30, 30, 40)); break;
            case 5: c_.Disc(cx - 8, cy, 26, k); for (int32_t i = 0; i < 22; ++i) { c_.Fill(cx + 12 + i, cy - i, 2, 2 * i + 1, k); } c_.Disc(cx - 18, cy - 6, 4, Rgb(30, 30, 40)); break;
            case 6: for (int32_t r = 0; r < 4; ++r) { for (int32_t i = 0; i < 4 - r; ++i) { c_.Disc(cx - (3 - r) * 11 + i * 22, cy - 24 + r * 18, 11, k); } } break;
            case 7: c_.Fill(cx - 30, cy - 4, 60, 38, k); for (int32_t i = 0; i < 34; ++i) { c_.Fill(cx - i, cy - 36 + i, 2 * i + 1, 1, dark); } c_.Fill(cx - 8, cy + 12, 16, 22, Rgb(90, 60, 40)); break;
            case 8: c_.Round(cx - 26, cy - 24, 52, 58, 8, k); c_.Fill(cx - 30, cy - 34, 60, 14, dark); break;
            case 9: for (int32_t i = 0; i < 34; ++i) { c_.Fill(cx - i, cy - 34 + i, 2 * i, 1, k); c_.Fill(cx - i, cy + 34 - i, 2 * i, 1, k); } c_.Line(cx, cy + 34, cx + 20, cy + 60, dark, 2); break;
            case 10: c_.Disc(cx, cy + 12, 26, Rgb(250, 210, 170)); for (int32_t i = 0; i < 3; ++i) { c_.Fill(cx - 24 + i * 18, cy - 26, 12, 22, k); } c_.Disc(cx - 8, cy + 10, 3, Rgb(30, 30, 40)); c_.Disc(cx + 8, cy + 10, 3, Rgb(30, 30, 40)); break;
            case 11: c_.Disc(cx - 14, cy - 18, 18, Rgb(190, 200, 215)); c_.Disc(cx + 14, cy - 18, 18, Rgb(190, 200, 215)); for (int32_t i = 0; i < 5; ++i) { c_.Fill(cx - 24 + i * 12, cy + 6 + (i % 2) * 8, 3, 14, k); } break;
            case 12: c_.Disc(cx, cy, 22, k); for (int32_t i = 0; i < 8; ++i) { const float t = static_cast<float>(i) * m::kTwoPi / 8.0F; c_.Line(cx + static_cast<int32_t>(m::Cos(t) * 28.0F), cy + static_cast<int32_t>(m::Sin(t) * 28.0F), cx + static_cast<int32_t>(m::Cos(t) * 40.0F), cy + static_cast<int32_t>(m::Sin(t) * 40.0F), k, 3); } break;
            case 13: c_.Disc(cx, cy - 12, 28, k); c_.Fill(cx - 7, cy + 12, 14, 26, Rgb(120, 80, 50)); break;
            case 14: for (int32_t i = 0; i < 30; ++i) { c_.Fill(cx - i, cy - 30 + i, 2 * i + 1, 1, k); } c_.Fill(cx - 10, cy, 20, 34, k); break;
            default: c_.Round(cx - 38, cy - 22, 76, 44, 6, k); c_.Disc(cx - 22, cy + 24, 9, Rgb(40, 40, 50)); c_.Disc(cx + 22, cy + 24, 9, Rgb(40, 40, 50)); c_.Fill(cx + 12, cy - 16, 20, 14, Rgb(200, 230, 250)); break;
        }
    }
    void Draw() {
        c_.Begin(); const L& w = kL[i_]; const Color ink = Rgb(50, 40, 70); const char big[2] = {static_cast<char>('A' + i_), '\0'};
        c_.Round(210, 6, 80, 28, 6, Rgb(240, 150, 90)); c_.Text(250, 10, quiz_ ? "Learn" : "Quiz", kWhite, kSmall, true);
        if (!quiz_) { c_.Text(60, 40, big, Rgb(230, 90, 90), kTitle, true); Pic(w.pic, 190, 100, w.col); c_.Text(kW / 2, 180, w.word, ink, kLarge, true); c_.Text(20, 214, "< back", kGrey); c_.Text(230, 214, "next >", kGrey); }
        else { Pic(w.pic, 148, 72, w.col); for (int32_t s = 0; s < 3; ++s) { const bool showRight = fb_ > 0 && s == slot_; c_.Round(6 + s * 98, 142, 88, 88, 12, showRight ? Rgb(120, 220, 140) : Rgb(120, 170, 240)); const char t[2] = {static_cast<char>('A' + c_i_[s]), '\0'}; c_.Text(50 + s * 98, 168, t, kWhite, kTitle, true); } c_.Number(20, 10, score_, ink, kMedium); }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t i_{}, c_i_[3]{}, slot_{}, fb_{}, score_{}; bool quiz_{}, right_{};
};
}  // namespace
PORTKIT_MAIN(Game)
