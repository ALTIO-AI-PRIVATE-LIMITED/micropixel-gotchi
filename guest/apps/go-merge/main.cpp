// SPDX-License-Identifier: Apache-2.0
//
// Merge Cubes: slide to merge equal numbers and reach 2048.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(30, 26, 40)), p_(a) { best_ = p_.Get("best"); New(); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { sx_ = x; sy_ = y; used_ = false; if (over_) { New(); used_ = true; } }
    void Drag(int32_t x, int32_t y) { if (used_) { return; } const int32_t dx = x - sx_, dy = y - sy_, ax = m::Abs(dx), ay = m::Abs(dy); if (ax < 22 && ay < 22) { return; } used_ = true; Slide(ax > ay ? (dx > 0 ? 0 : 2) : (dy > 0 ? 1 : 3)); }
    void Button(KeyCode k) { if (k == kBoot) { New(); } else if (k == kVolUp) { Slide(0); } else { Slide(2); } }

   private:
    void New() { for (auto& r : g_) { for (auto& v : r) { v = 0U; } } score_ = 0U; over_ = false; Add(); Add(); }
    void Add() { int32_t free[16], n = 0; for (int32_t i = 0; i < 16; ++i) { if (g_[i / 4][i % 4] == 0U) { free[n++] = i; } } if (n == 0) { return; } const int32_t i = free[a_.random().Below(static_cast<uint32_t>(n))]; g_[i / 4][i % 4] = a_.random().Below(10U) == 0U ? 4U : 2U; }
    bool Line(uint32_t* l[4]) { uint32_t out[4]{}; int32_t n = 0; bool moved = false, can = true; for (int32_t i = 0; i < 4; ++i) { if (*l[i] == 0U) { continue; } if (n > 0 && can && out[n - 1] == *l[i]) { out[n - 1] *= 2U; score_ += out[n - 1]; can = false; } else { out[n++] = *l[i]; can = true; } } for (int32_t i = 0; i < 4; ++i) { if (*l[i] != out[i]) { moved = true; *l[i] = out[i]; } } return moved; }
    void Slide(int32_t d) {
        bool moved = false;
        for (int32_t k = 0; k < 4; ++k) { uint32_t* l[4]; for (int32_t i = 0; i < 4; ++i) { l[i] = d == 0 ? &g_[k][3 - i] : (d == 2 ? &g_[k][i] : (d == 1 ? &g_[3 - i][k] : &g_[i][k])); } moved = Line(l) || moved; }
        if (!moved) { return; } Add(); Beep(a_, 420U, 25U, 110U); if (score_ > best_) { best_ = score_; p_.Set("best", best_); }
        bool can = false; for (int32_t y = 0; y < 4; ++y) { for (int32_t x = 0; x < 4; ++x) { can = can || g_[y][x] == 0U || (x < 3 && g_[y][x] == g_[y][x + 1]) || (y < 3 && g_[y][x] == g_[y + 1][x]); } } over_ = !can;
    }
    void Draw() {
        c_.Begin(); constexpr int32_t kS = 56;
        c_.Text(12, 18, "SCORE", kGrey); c_.Number(12, 36, static_cast<int32_t>(score_), kWhite, kMedium); c_.Text(12, 80, "BEST", kGrey); c_.Number(12, 98, static_cast<int32_t>(best_), kWhite, kMedium);
        c_.Text(12, 200, "swipe", kDim);
        for (int32_t y = 0; y < 4; ++y) { for (int32_t x = 0; x < 4; ++x) { const uint32_t v = g_[y][x]; int32_t lvl = 0; for (uint32_t t = v; t > 1U; t >>= 1U) { ++lvl; } const Color k = v == 0U ? Rgb(52, 46, 66) : Color::Mix(Rgb(255, 90, 80), Rgb(120, 190, 255), static_cast<uint8_t>(m::Min(lvl * 24, 255))); c_.Round(72 + x * kS + 2, 4 + y * kS + 2, kS - 4, kS - 4, 6, k); if (v != 0U) { c_.Number(72 + x * kS + kS / 2, 4 + y * kS + 16, static_cast<int32_t>(v), kWhite, v >= 1000U ? kSmall : kMedium, true); } } }
        if (over_) { Banner(c_, "NO MOVES", "Tap for a new board", Rgb(255, 130, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; uint32_t g_[4][4]{}, score_{}, best_{}; int32_t sx_{}, sy_{}; bool used_{}, over_{};
};
}  // namespace
PORTKIT_MAIN(Game)
