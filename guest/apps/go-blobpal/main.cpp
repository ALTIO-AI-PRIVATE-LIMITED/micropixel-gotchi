// SPDX-License-Identifier: Apache-2.0
//
// Blob Pal: raise a small blob from an egg. Feed it, play with it, let it nap and keep its room clean.
// Stats are saved on the Gotchi. Time only passes while the app is open.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
const Box kBtn[4] = {{4, 200, 69, 36}, {77, 200, 69, 36}, {150, 200, 69, 36}, {223, 200, 69, 36}};
const char* const kBtnName[4] = {"Feed", "Play", "Nap", "Clean"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(150, 210, 240)), p_(a) {
        if (p_.Get("made") != 0U) { food_ = static_cast<int32_t>(p_.Get("food")); joy_ = static_cast<int32_t>(p_.Get("joy")); rest_ = static_cast<int32_t>(p_.Get("rest")); age_ = p_.Get("age"); mess_ = static_cast<int32_t>(p_.Get("mess")); }
    }
    void Tick(uint32_t ms) {
        t_ += ms; cl_ += ms;
        if (cl_ >= 1000U) { cl_ = 0U; Second(); }
        if (say_ > 0) { --say_; }
        if (ball_ >= 0) { BallStep(); }
        Draw();
    }
    void Press(int32_t x, int32_t y) {
        if (ball_ >= 0) { const int32_t bx = static_cast<int32_t>(bx_), by = static_cast<int32_t>(by_); if ((x - bx) * (x - bx) + (y - by) * (y - by) < 1200) { bv_ = -9.0F; bs_ = (x < bx ? 2.5F : -2.5F); ++ball_; Beep(a_, 620U, 40U); } return; }
        for (int32_t i = 0; i < 4; ++i) { if (kBtn[i].Has(x, y)) { Do(i); return; } }
        if (x > 90 && x < 206 && y > 70 && y < 190 && !sleeping_) { joy_ = Add(joy_, 2); Say("purr"); Beep(a_, 300U, 30U, 90U); }
    }
    void Button(KeyCode k) { if (k == kBoot) { Do(0); } else if (k == kVolUp) { Do(1); } else if (k == kVolDown) { Do(2); } }

   private:
    static int32_t Add(int32_t v, int32_t d) { const int32_t n = v + d; return n > 100 ? 100 : (n < 0 ? 0 : n); }
    void Say(const char* s) { text_ = s; say_ = 50; }
    uint32_t stage() const { return age_ < 60U ? 0U : (age_ < 600U ? 1U : 2U); }
    void Do(int32_t i) {
        if (stage() == 0U) { age_ += 8U; Say("crack"); Beep(a_, 500U, 40U); return; }
        if (sleeping_ && i != 2) { Say("shh"); return; }
        if (i == 0) { if (food_ > 92) { Say("so full"); return; } food_ = Add(food_, 22); Say("yum"); munch_ = 40; Beep(a_, 440U, 70U); }
        if (i == 1) { ball_ = 0; bx_ = 148.0F; by_ = 60.0F; bv_ = 0.0F; bs_ = 2.0F; }
        if (i == 2) { sleeping_ = !sleeping_; c_.SetBackground(sleeping_ ? Rgb(26, 30, 66) : Rgb(150, 210, 240)); Say(sleeping_ ? "night" : "morning"); }
        if (i == 3) { if (mess_ > 0) { mess_ = 0; joy_ = Add(joy_, 6); Say("fresh"); Beep(a_, 880U, 70U); } else { Say("all clean"); } }
    }
    void BallStep() {
        bv_ += 0.5F; by_ += bv_; bx_ += bs_;
        if (bx_ < 24.0F || bx_ > 272.0F) { bs_ = -bs_; }
        if (by_ > 196.0F) { joy_ = Add(joy_, ball_ * 4); rest_ = Add(rest_, -4); Say(ball_ > 2 ? "again" : "fun"); ball_ = -1; }
    }
    void Second() {
        ++age_; if (stage() == 0U) { return; }
        ++s_;
        if (munch_ > 0) { munch_ = 0; }
        if (sleeping_) { rest_ = Add(rest_, 2); if (rest_ == 100) { sleeping_ = false; c_.SetBackground(Rgb(150, 210, 240)); Say("morning"); } }
        else if (s_ % 6 == 0) { rest_ = Add(rest_, -1); joy_ = Add(joy_, mess_ > 0 ? -2 : -1); }
        if (s_ % 8 == 0) { food_ = Add(food_, -1); }
        if (s_ % 90 == 0 && mess_ < 3) { ++mess_; }
        if (s_ % 10 == 0) { Save(); }
    }
    void Save() { p_.Set("made", 1U); p_.Set("food", static_cast<uint32_t>(food_)); p_.Set("joy", static_cast<uint32_t>(joy_)); p_.Set("rest", static_cast<uint32_t>(rest_)); p_.Set("age", age_); p_.Set("mess", static_cast<uint32_t>(mess_)); }
    void Bar(int32_t x, const char* name, int32_t v, Color k) { c_.Text(x, 6, name, sleeping_ ? kWhite : Rgb(20, 30, 60)); c_.Fill(x, 24, 80, 9, Rgb(60, 80, 110)); c_.Fill(x, 24, v * 80 / 100, 9, v < 25 ? Rgb(240, 70, 70) : k); }
    void Draw() {
        c_.Begin();
        const Color ground = sleeping_ ? Rgb(40, 50, 90) : Rgb(130, 200, 120);
        c_.Fill(0, 170, kW, 70, ground);
        Bar(10, "Food", food_, Rgb(255, 170, 70)); Bar(108, "Joy", joy_, Rgb(255, 110, 160)); Bar(206, "Rest", rest_, Rgb(90, 130, 255));
        for (int32_t i = 0; i < mess_; ++i) { c_.Disc(40 + i * 28, 186, 7, Rgb(120, 86, 60)); c_.Disc(40 + i * 28, 178, 4, Rgb(140, 100, 70)); }
        const int32_t bob = static_cast<int32_t>(m::Sin(static_cast<float>(t_) * 0.004F) * 5.0F);
        if (stage() == 0U) {
            c_.Disc(148, 128 + bob / 2, 42, Rgb(250, 240, 210)); c_.Disc(136, 112 + bob / 2, 6, Rgb(255, 170, 120)); c_.Disc(164, 140 + bob / 2, 8, Rgb(120, 200, 200));
            c_.Text(kW / 2, 178, "Tap a button to warm the egg", Rgb(30, 40, 70), kSmall, true);
        } else { DrawBlob(bob); }
        if (ball_ >= 0) { c_.Disc(static_cast<int32_t>(bx_), static_cast<int32_t>(by_), 13, Rgb(255, 90, 90)); c_.Text(kW / 2, 44, "Tap the ball", Rgb(30, 40, 70), kSmall, true); }
        if (say_ > 0 && text_ != nullptr) { c_.Round(196, 52, 92, 26, 8, kWhite); c_.Text(242, 57, text_, Rgb(30, 30, 50), kSmall, true); }
        for (int32_t i = 0; i < 4; ++i) { const Box& b = kBtn[i]; c_.Round(b.x, b.y, b.w, b.h, 8, Rgb(40, 52, 96)); c_.Text(b.x + b.w / 2, b.y + 9, kBtnName[i], kWhite, kSmall, true); }
        c_.Present();
    }
    void DrawBlob(int32_t bob) {
        const int32_t r = stage() == 1U ? 34 : 50, cy = 170 - r + bob / 2;
        const Color body = joy_ < 25 ? Rgb(150, 160, 190) : Rgb(120, 230, 180);
        c_.Disc(148, cy, r, body); c_.Disc(148, cy + r / 2, r - 6, body);
        if (sleeping_) { c_.Fill(120, cy - 2, 18, 4, Rgb(30, 40, 60)); c_.Fill(158, cy - 2, 18, 4, Rgb(30, 40, 60)); c_.Text(214, cy - 40, "z", kWhite, kLarge); return; }
        c_.Disc(130, cy - 4, 8, kWhite); c_.Disc(166, cy - 4, 8, kWhite); c_.Disc(131, cy - 3, 4, Rgb(20, 24, 40)); c_.Disc(167, cy - 3, 4, Rgb(20, 24, 40));
        if (munch_ > 0) { c_.Disc(148, cy + 18, 6 + static_cast<int32_t>(t_ / 80U) % 4, Rgb(90, 50, 70)); }
        else if (joy_ > 40 && food_ > 20) { for (int32_t x = -12; x <= 12; ++x) { c_.Fill(148 + x, cy + 22 - x * x / 24, 2, 3, Rgb(40, 60, 70)); } }
        else { for (int32_t x = -12; x <= 12; ++x) { c_.Fill(148 + x, cy + 16 + x * x / 24, 2, 3, Rgb(40, 60, 70)); } }
    }
    Application& a_; Canvas c_; Prefs p_;
    int32_t food_{70}, joy_{70}, rest_{80}, mess_{}, say_{}, munch_{}, ball_{-1}, s_{}; uint32_t age_{}, t_{}, cl_{}; bool sleeping_{};
    float bx_{}, by_{}, bv_{}, bs_{}; const char* text_{};
};
}  // namespace
PORTKIT_MAIN(Game)
