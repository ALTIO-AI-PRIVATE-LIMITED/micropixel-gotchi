// SPDX-License-Identifier: Apache-2.0
//
// Lucky Lounge: slots, high-low and a spin wheel with play chips. Nothing to buy and nothing to win.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kSym[] = {"7","BAR","*","$","#"}; constexpr Color kK[] = {Rgb(255,70,80),Rgb(90,190,255),Rgb(255,220,90),Rgb(120,230,130),Rgb(220,130,255)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(20, 44, 34)), p_(a) { chips_ = p_.Get("chips", 200U); r_[0] = r_[1] = r_[2] = 0; New(); }
    void Tick(uint32_t) { ++f_; Step(); Draw(); }
    void Press(int32_t x, int32_t y) {
        if (mode_ == 0) { if (y > 56) { mode_ = m::Clamp((y - 56) / 60, 0, 2) + 1; msg_ = "Good luck"; } return; }
        if (y < 40) { mode_ = 0; return; }
        if (mode_ == 1) { Spin(); } else if (mode_ == 2) { Guess(x > kW / 2); } else { Wheel(); }
    }
    void Button(KeyCode k) { if (k == kBoot) { mode_ = 0; } }

   private:
    bool Bet(uint32_t n) { if (chips_ < n) { chips_ = 200U; msg_ = "Refilled with 200 chips"; Save(); return false; } chips_ -= n; return true; }
    void Save() { p_.Set("chips", chips_); }
    void Spin() { if (spin_ > 0 || !Bet(10U)) { return; } spin_ = 30; msg_ = "Spinning"; }
    void New() { card_ = 1 + static_cast<int32_t>(a_.random().Below(13U)); }
    void Guess(bool high) { if (!Bet(10U)) { return; } const int32_t was = card_; New(); const bool win = high ? card_ > was : card_ < was; if (card_ == was) { chips_ += 10U; msg_ = "Same card, bet back"; } else if (win) { chips_ += 20U; msg_ = "Right, plus 10"; Beep(a_, 880U, 90U); } else { msg_ = "Wrong, minus 10"; Beep(a_, 200U, 120U); } Save(); }
    void Wheel() { if (wv_ > 0.0F || !Bet(10U)) { return; } wv_ = 0.34F + static_cast<float>(a_.random().Below(30U)) * 0.01F; msg_ = "Round it goes"; }
    void Step() {
        if (spin_ > 0) { --spin_; for (int32_t i = 0; i < 3; ++i) { if (spin_ > i * 8) { r_[i] = static_cast<int32_t>(a_.random().Below(5U)); } } if (spin_ == 0) { if (r_[0] == r_[1] && r_[1] == r_[2]) { const uint32_t w = r_[0] == 0 ? 300U : 80U; chips_ += w; msg_ = r_[0] == 0 ? "Sevens, plus 300" : "Three alike, plus 80"; Beep(a_, 988U, 300U); } else if (r_[0] == r_[1] || r_[1] == r_[2]) { chips_ += 20U; msg_ = "Pair, plus 20"; Beep(a_, 660U, 100U); } else { msg_ = "No luck"; } Save(); } }
        if (wv_ > 0.0F) { wa_ += wv_; wv_ -= 0.0028F; if (wv_ <= 0.0F) { wv_ = 0.0F; float t = m::WrapAngle(wa_); if (t < 0.0F) { t += m::kTwoPi; } const int32_t s = static_cast<int32_t>(t / m::kTwoPi * 8.0F) % 8; const uint32_t pay[] = {0U, 20U, 0U, 15U, 0U, 50U, 5U, 0U}; chips_ += pay[s]; msg_ = pay[s] == 0U ? "Nothing this time" : "A win"; if (pay[s] != 0U) { Beep(a_, 784U, 150U); } Save(); } }
    }
    void Draw() {
        c_.Begin();
        c_.Text(10, 10, "Chips", kGrey); c_.Number(54, 10, static_cast<int32_t>(chips_), Rgb(255, 220, 110), kMedium);
        if (mode_ == 0) {
            const char* const names[] = {"Slots","High or Low","Spin Wheel"}; for (int32_t i = 0; i < 3; ++i) { c_.Round(20, 58 + i * 60, kW - 40, 52, 10, Rgb(40, 110, 80)); c_.Text(kW / 2, 72 + i * 60, names[i], kWhite, kMedium, true); }
        } else {
            c_.Round(kW - 70, 6, 64, 30, 6, Rgb(40, 110, 80)); c_.Text(kW - 38, 12, "Menu", kWhite, kSmall, true);
            if (mode_ == 1) { for (int32_t i = 0; i < 3; ++i) { c_.Round(22 + i * 88, 62, 76, 92, 8, Rgb(245, 240, 225)); c_.Text(60 + i * 88, 92, kSym[r_[i]], Color::Mix(kK[r_[i]], Rgb(0, 0, 0), 200U), kLarge, true); } c_.Text(kW / 2, 172, "Tap to spin, 10 chips", kGrey, kSmall, true); }
            else if (mode_ == 2) { char s[8]; const int32_t v = card_; if (v == 1) { s[0] = 'A'; s[1] = '\0'; } else if (v > 10) { s[0] = "JQK"[v - 11]; s[1] = '\0'; } else { Canvas::Format(s, v); } c_.Round(108, 50, 80, 116, 8, Rgb(245, 240, 225)); c_.Text(148, 90, s, Rgb(40, 40, 60), kTitle, true); c_.Text(70, 96, "LOWER", kWhite, kSmall, true); c_.Text(226, 96, "HIGHER", kWhite, kSmall, true); c_.Text(kW / 2, 172, "Tap a side, 10 chips", kGrey, kSmall, true); }
            else { for (int32_t i = 0; i < 8; ++i) { const float t = wa_ + static_cast<float>(i) * m::kTwoPi / 8.0F; c_.Disc(148 + static_cast<int32_t>(m::Cos(t) * 56.0F), 108 + static_cast<int32_t>(m::Sin(t) * 56.0F), 13, i % 2 == 0 ? Rgb(200, 60, 80) : Rgb(50, 50, 70)); } c_.Disc(148, 108, 22, Rgb(255, 214, 110)); c_.Fill(204, 104, 22, 8, kWhite); c_.Text(kW / 2, 176, "Tap to spin", kGrey, kSmall, true); }
            c_.Text(kW / 2, 202, msg_, kWhite, kMedium, true);
        }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; uint32_t chips_{}; int32_t mode_{}, f_{}, spin_{}, r_[3]{}, card_{}; float wa_{}, wv_{}; const char* msg_{""};
};
}  // namespace
PORTKIT_MAIN(Game)
