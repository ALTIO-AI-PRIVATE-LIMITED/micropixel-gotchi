// SPDX-License-Identifier: Apache-2.0
//
// Nine Grid: fill every row, column and box with 1 to 9. Three difficulties.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(238, 234, 222), 800U, 48U) { New(); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) {
        if (won_) { New(); return; }
        if (x < kS * 9 + 4) { const int32_t cx = (x - 4) / kS, cy = (y - 6) / kS; if (cx >= 0 && cy >= 0 && cx < 9 && cy < 9) { sx_ = cx; sy_ = cy; } return; }
        const int32_t col = (x - 226) / 23, row = (y - 16) / 44; if (col < 0 || col > 2 || row < 0) { return; }
        if (row >= 3) { Put(0); return; } Put(row * 3 + col + 1);
    }
    void Button(KeyCode k) { if (k == kBoot) { level_ = (level_ + 1) % 3; New(); } else { sx_ = (sx_ + (k == kVolUp ? 1 : 8)) % 9; if (sx_ == 0 && k == kVolUp) { sy_ = (sy_ + 1) % 9; } } }

   private:
    static constexpr int32_t kS = 25;
    void New() {
        int8_t map[10]; for (int32_t i = 1; i <= 9; ++i) { map[i] = static_cast<int8_t>(i); } for (int32_t i = 9; i > 1; --i) { const int32_t j = 1 + static_cast<int32_t>(a_.random().Below(static_cast<uint32_t>(i))); const int8_t t = map[i]; map[i] = map[j]; map[j] = t; }
        for (int32_t y = 0; y < 9; ++y) { for (int32_t x = 0; x < 9; ++x) { s_[y][x] = map[1 + (y * 3 + y / 3 + x) % 9]; } }
        for (int32_t n = 0; n < 12; ++n) { const int32_t band = static_cast<int32_t>(a_.random().Below(3U)) * 3, a = band + static_cast<int32_t>(a_.random().Below(3U)), b = band + static_cast<int32_t>(a_.random().Below(3U)); for (int32_t x = 0; x < 9; ++x) { const int8_t t = s_[a][x]; s_[a][x] = s_[b][x]; s_[b][x] = t; } for (int32_t y = 0; y < 9; ++y) { const int8_t t = s_[y][a]; s_[y][a] = s_[y][b]; s_[y][b] = t; } }
        const int32_t holes = 34 + level_ * 10; for (int32_t y = 0; y < 9; ++y) { for (int32_t x = 0; x < 9; ++x) { g_[y][x] = s_[y][x]; fix_[y][x] = true; } } for (int32_t n = 0; n < holes;) { const auto x = static_cast<int32_t>(a_.random().Below(9U)), y = static_cast<int32_t>(a_.random().Below(9U)); if (fix_[y][x]) { fix_[y][x] = false; g_[y][x] = 0; ++n; } } won_ = false;
    }
    bool Bad(int32_t x, int32_t y) const { const int8_t v = g_[y][x]; if (v == 0) { return false; } for (int32_t i = 0; i < 9; ++i) { if ((i != x && g_[y][i] == v) || (i != y && g_[i][x] == v)) { return true; } } for (int32_t a = 0; a < 3; ++a) { for (int32_t b = 0; b < 3; ++b) { const int32_t cx = x / 3 * 3 + a, cy = y / 3 * 3 + b; if ((cx != x || cy != y) && g_[cy][cx] == v) { return true; } } } return false; }
    void Put(int32_t v) { if (fix_[sy_][sx_]) { return; } g_[sy_][sx_] = static_cast<int8_t>(v); Beep(a_, 500U + static_cast<uint32_t>(v) * 30U, 20U, 90U); bool ok = true; for (int32_t y = 0; y < 9; ++y) { for (int32_t x = 0; x < 9; ++x) { ok = ok && g_[y][x] != 0 && !Bad(x, y); } } if (ok) { won_ = true; Beep(a_, 880U, 300U); } }
    void Draw() {
        c_.Begin();
        for (int32_t y = 0; y < 9; ++y) { for (int32_t x = 0; x < 9; ++x) { const int32_t px = 4 + x * kS, py = 6 + y * kS; const bool sel = x == sx_ && y == sy_, same = g_[y][x] != 0 && g_[y][x] == g_[sy_][sx_]; c_.Fill(px, py, kS - 1, kS - 1, sel ? Rgb(120, 180, 255) : (same ? Rgb(200, 220, 250) : kWhite)); } }
        for (int32_t i = 0; i <= 9; i += 3) { c_.Fill(3 + i * kS, 5, 2, 9 * kS + 1, Rgb(40, 40, 60)); c_.Fill(3, 5 + i * kS, 9 * kS + 2, 2, Rgb(40, 40, 60)); }
        Numbers();
        c_.Present();
    }
    void Numbers() {
        // Digits drawn as small seven-segment shapes so the whole board fits.
        for (int32_t y = 0; y < 9; ++y) { for (int32_t x = 0; x < 9; ++x) { if (g_[y][x] != 0) { Seg(4 + x * kS + 7, 6 + y * kS + 4, g_[y][x], fix_[y][x] ? Rgb(30, 30, 50) : (Bad(x, y) ? Rgb(220, 50, 60) : Rgb(40, 90, 200))); } } }
        for (int32_t i = 0; i < 9; ++i) { const int32_t bx = 228 + (i % 3) * 23, by = 16 + (i / 3) * 44; c_.Round(bx, by, 21, 40, 4, Rgb(60, 70, 110)); const char s[2] = {static_cast<char>('1' + i), '\0'}; c_.Text(bx + 11, by + 12, s, kWhite, kSmall, true); }
        c_.Round(228, 148, 67, 40, 4, Rgb(190, 90, 90)); c_.Text(261, 160, "Clear", kWhite, kSmall, true);
        const char* const lv[] = {"Easy","Medium","Hard"}; c_.Text(261, 200, lv[level_], Rgb(40, 40, 60), kSmall, true);
        if (won_) { Banner(c_, "SOLVED", "Tap for a new puzzle", Rgb(120, 180, 255)); }
    }
    void Seg(int32_t x, int32_t y, int32_t d, Color k) { constexpr uint8_t kM[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F}; const uint8_t b = kM[d]; constexpr int32_t w = 9, h = 7; if (b & 1U) { c_.Fill(x, y, w, 2, k); } if (b & 2U) { c_.Fill(x + w - 2, y, 2, h + 1, k); } if (b & 4U) { c_.Fill(x + w - 2, y + h, 2, h + 1, k); } if (b & 8U) { c_.Fill(x, y + 2 * h, w, 2, k); } if (b & 16U) { c_.Fill(x, y + h, 2, h + 1, k); } if (b & 32U) { c_.Fill(x, y, 2, h + 1, k); } if (b & 64U) { c_.Fill(x, y + h, w, 2, k); } }
    Application& a_; Canvas c_; int8_t s_[9][9]{}, g_[9][9]{}; bool fix_[9][9]{}, won_{}; int32_t sx_{}, sy_{}, level_{};
};
}  // namespace
PORTKIT_MAIN(Game)
