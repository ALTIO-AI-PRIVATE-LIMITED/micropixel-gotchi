// SPDX-License-Identifier: Apache-2.0
//
// Five Guess: find the hidden five-letter word in six tries.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char kWords[] = "APPLEBRAVECHAIRDANCEEAGLEFLAMEGRAPEHOUSEIVORYJELLYKNIFELEMONMANGONOBLEOCEANPIANOQUEENRIVERSTONETIGERUNCLEVOICEWATERYOUTHZEBRABREADCLOUDDREAMEARTHFIELDGHOSTHEARTINDEXJUICEKINGSLIGHTMONEYNIGHTOLIVEPLANTQUICKROBOTSMILETRAINUNITYVALUEWORLDYACHTBLINKCRANEDRIFTEMBERFROSTGLASSHONEYISLANDJOKERKOALALASERMOUSENORTHORBITPEARLQUILTRAINYSOLARTOWERULTRAVIVIDWHEATYEASTZESTY";
constexpr int32_t kCount = (sizeof(kWords) - 1) / 5;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 20, 32), 800U, 44U) { New(); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { if (over_) { New(); return; } const char k = Keyboard::Press(x, y); if (k == 0 || k == ' ') { return; } if (k == '\b') { if (n_ > 0) { --n_; } } else if (k == '\n') { Enter(); } else if (n_ < 5) { g_[row_][n_++] = k; Beep(a_, 700U, 12U, 80U); } }
    void Button(KeyCode k) { if (k == kBoot) { New(); } }

   private:
    void New() { const auto i = a_.random().Below(kCount); for (uint32_t k = 0; k < 5U; ++k) { w_[k] = kWords[i * 5U + k]; } for (auto& r : g_) { for (auto& ch : r) { ch = 0; } } for (auto& r : st_) { for (auto& s : r) { s = 0; } } row_ = n_ = 0; over_ = false; win_ = false; for (auto& v : used_) { v = 0; } }
    void Enter() {
        if (n_ < 5) { return; } int32_t left[26]{}; for (int32_t i = 0; i < 5; ++i) { ++left[w_[i] - 'A']; }
        for (int32_t i = 0; i < 5; ++i) { if (g_[row_][i] == w_[i]) { st_[row_][i] = 3; --left[w_[i] - 'A']; } }
        for (int32_t i = 0; i < 5; ++i) { if (st_[row_][i] == 0) { const int32_t c = g_[row_][i] - 'A'; if (left[c] > 0) { st_[row_][i] = 2; --left[c]; } else { st_[row_][i] = 1; } } }
        bool all = true; for (int32_t i = 0; i < 5; ++i) { all = all && st_[row_][i] == 3; const int32_t c = g_[row_][i] - 'A'; used_[c] = m::Max<uint8_t>(used_[c], st_[row_][i]); }
        ++row_; n_ = 0; if (all) { over_ = win_ = true; Beep(a_, 880U, 260U); } else if (row_ >= 6) { over_ = true; Beep(a_, 160U, 260U); } else { Beep(a_, 500U, 30U); }
    }
    void Draw() {
        c_.Begin();
        constexpr Color k[] = {Rgb(44, 48, 66), Rgb(70, 74, 90), Rgb(200, 160, 50), Rgb(70, 170, 90)};
        for (int32_t r = 0; r < 6; ++r) { for (int32_t i = 0; i < 5; ++i) { const int32_t x = 92 + i * 23, y = 3 + r * 20; c_.Fill(x, y, 21, 18, k[st_[r][i]]); if (g_[r][i] != 0) { const char s[2] = {g_[r][i], '\0'}; c_.Text(x + 11, y + 2, s, kWhite, kSmall, true); } } }
        if (over_) { char s[6]; for (int32_t i = 0; i < 5; ++i) { s[i] = w_[i]; } s[5] = '\0'; c_.Text(46, 34, win_ ? "Got it" : "Word:", kWhite, kSmall, true); c_.Text(46, 54, s, Rgb(255, 214, 110), kSmall, true); c_.Text(46, 90, "Tap", kGrey, kSmall, true); }
        Keyboard::Draw(c_, Rgb(70, 76, 104), kWhite);
        c_.Present();
    }
    Application& a_; Canvas c_; char w_[5]{}, g_[6][5]{}; uint8_t st_[6][5]{}, used_[26]{}; int32_t row_{}, n_{}; bool over_{}, win_{};
};
}  // namespace
PORTKIT_MAIN(Game)
