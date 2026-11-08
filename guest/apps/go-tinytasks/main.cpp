// SPDX-License-Identifier: Apache-2.0
//
// Tiny Tasks: a short to-do list you tick off with a tap. Saved on the Gotchi.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kMax = 10, kLen = 30, kRow = 34;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 26, 34), 500U, 44U), p_(a) { (void)p_.Load("t", reinterpret_cast<uint8_t*>(t_), sizeof(t_)); (void)p_.Load("d", d_, kMax); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) {
        if (edit_ >= 0) { if (y >= Keyboard::kTop) { Key(Keyboard::Press(x, y)); } return; }
        if (y > 206) { if (x < 98) { top_ = m::Max(top_ - 5, 0); } else if (x < 198) { Add(); } else if (top_ + 5 < kMax) { top_ += 5; } return; }
        const int32_t i = top_ + (y - 8) / kRow; if (i >= 0 && i < kMax && t_[i][0] != '\0') { if (x > 250) { Remove(i); } else { d_[i] = d_[i] == 0U ? 1U : 0U; Save(); Beep(a_, d_[i] != 0U ? 880U : 400U, 40U); } }
    }

   private:
    void Add() { for (int32_t i = 0; i < kMax; ++i) { if (t_[i][0] == '\0') { edit_ = i; d_[i] = 0U; return; } } Beep(a_, 180U, 80U); }
    void Remove(int32_t i) { for (; i + 1 < kMax; ++i) { for (int32_t c = 0; c < kLen; ++c) { t_[i][c] = t_[i + 1][c]; } d_[i] = d_[i + 1]; } t_[kMax - 1][0] = '\0'; d_[kMax - 1] = 0U; Save(); }
    void Save() { p_.Save("t", reinterpret_cast<const uint8_t*>(t_), sizeof(t_)); p_.Save("d", d_, kMax); }
    void Key(char k) { if (k == 0) { return; } char* s = t_[edit_]; int32_t l = 0; while (s[l] != '\0') { ++l; } if (k == '\n') { edit_ = -1; Save(); return; } if (k == '\b') { if (l > 0) { s[l - 1] = '\0'; } return; } if (l < kLen - 1) { s[l] = k; s[l + 1] = '\0'; } }
    void Draw() {
        c_.Begin();
        if (edit_ >= 0) { c_.Fill(6, 40, kW - 12, 50, kWhite); c_.Text(14, 54, t_[edit_], Rgb(30, 30, 50), kMedium); c_.Text(14, 12, "New task", kGrey, kMedium); Keyboard::Draw(c_, Rgb(70, 90, 110), kWhite); }
        else { for (int32_t i = 0; i < 5; ++i) { const int32_t idx = top_ + i, y = 8 + i * kRow; if (idx >= kMax || t_[idx][0] == '\0') { continue; } c_.Round(6, y, kW - 12, kRow - 4, 6, Rgb(40, 56, 72)); c_.Frame(14, y + 7, 16, 16, kWhite, 2); if (d_[idx] != 0U) { c_.Fill(17, y + 10, 10, 10, Rgb(120, 230, 150)); } c_.Text(38, y + 7, t_[idx], d_[idx] != 0U ? kGrey : kWhite); c_.Text(270, y + 7, "x", Rgb(255, 130, 130), kSmall, true); } c_.Round(4, 210, 90, 26, 6, Rgb(50, 66, 86)); c_.Text(49, 214, "Up", kWhite, kSmall, true); c_.Round(102, 210, 92, 26, 6, Rgb(70, 150, 110)); c_.Text(148, 214, "Add", kWhite, kSmall, true); c_.Round(202, 210, 90, 26, 6, Rgb(50, 66, 86)); c_.Text(247, 214, "Down", kWhite, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; char t_[kMax][kLen]{}; uint8_t d_[kMax]{}; int32_t edit_{-1}, top_{};
};
}  // namespace
PORTKIT_MAIN(Game)
