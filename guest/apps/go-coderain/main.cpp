// SPDX-License-Identifier: Apache-2.0
//
// Code Rain: falling glyphs, signal glitch and retro bars. Tap to change the screen.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kCols = 24, kRows = 17, kCW = 12, kCH = 14;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(0, 0, 0), 1500U, 3U) { for (auto& h : head_) { h = static_cast<int32_t>(a_.random().Below(kRows * 3)) - kRows * 2; } for (auto& s : speed_) { s = 1 + static_cast<int32_t>(a_.random().Below(3U)); } }
    void Tick(uint32_t) { ++f_; c_.Begin(); if (mode_ == 0) { Rain(); } else if (mode_ == 1) { Glitch(); } else { Bars(); } c_.Present(); }
    void Tap(int32_t, int32_t) { mode_ = (mode_ + 1) % 3; }
    void Button(KeyCode) { mode_ = (mode_ + 1) % 3; }

   private:
    void Glyph(int32_t cx, int32_t cy, uint32_t h, Color k) { c_.Fill(cx, cy + static_cast<int32_t>(h % 4U) * 3, 8, 2, k); c_.Fill(cx + static_cast<int32_t>((h >> 3) % 3U) * 3, cy, 2, 12, k); c_.Fill(cx + static_cast<int32_t>((h >> 6) % 2U) * 6, cy + 2 + static_cast<int32_t>((h >> 8) % 2U) * 6, 2, 4, k); }
    void Rain() {
        if (f_ % 2 == 0) { for (int32_t x = 0; x < kCols; ++x) { head_[x] += speed_[x]; if (head_[x] - 16 > kRows * 1) { head_[x] = -static_cast<int32_t>(a_.random().Below(20U)); speed_[x] = 1 + static_cast<int32_t>(a_.random().Below(2U)); } } }
        for (int32_t x = 0; x < kCols; ++x) { for (int32_t i = 0; i < 13; ++i) { const int32_t y = head_[x] - i; if (y < 0 || y >= kRows) { continue; } const auto h = static_cast<uint32_t>((x * 73 + y * 151) ^ (((f_ / 9) + x * y) * (i < 3 ? 977 : 13))); Glyph(2 + x * kCW, y * kCH, h, i == 0 ? Rgb(220, 255, 230) : Rgb(0, static_cast<uint8_t>(235 - i * 15), 90)); } }
    }
    void Glitch() {
        for (int32_t i = 0; i < 26; ++i) { const auto y = static_cast<int32_t>(a_.random().Below(kH)); const auto k = a_.random().Below(5U); const Color col = k == 0U ? Rgb(255, 40, 120) : (k == 1U ? Rgb(40, 230, 255) : (k == 2U ? kWhite : Rgb(30, 30, 40))); c_.Fill(static_cast<int32_t>(a_.random().Below(200U)) - 40, y, 40 + static_cast<int32_t>(a_.random().Below(260U)), 1 + static_cast<int32_t>(a_.random().Below(12U)), col, 220U); }
        c_.Text(kW / 2 + static_cast<int32_t>(a_.random().Below(9U)) - 4, 104, "NO SIGNAL", kWhite, kLarge, true);
    }
    void Bars() { const int32_t off = f_ * 2 % 42; for (int32_t i = -1; i < 8; ++i) { const uint8_t v = static_cast<uint8_t>(80 + 170 * ((i + f_ / 12) % 3 == 0)); c_.Fill(0, i * 42 + off, kW, 40, Rgb(v, static_cast<uint8_t>(40 + i * 20), static_cast<uint8_t>(255 - v))); } for (int32_t x = 0; x < kW; x += 4) { c_.Fill(x, 0, 1, kH, Rgb(0, 0, 0), 90U); } }
    Application& a_; Canvas c_; int32_t head_[kCols]{}, speed_[kCols]{}, f_{}, mode_{};
};
}  // namespace
PORTKIT_MAIN(Game)
