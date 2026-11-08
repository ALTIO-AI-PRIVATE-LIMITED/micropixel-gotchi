// SPDX-License-Identifier: Apache-2.0
//
// Pixel Pad: paint 32 by 34 pixel art with eight colours. Saved on the Gotchi.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 32, kR = 34, kS = 7, kBytes = kC * kR / 2;
constexpr Color kPal[8] = {Rgb(20,20,28),kWhite,Rgb(240,70,80),Rgb(250,190,60),Rgb(90,210,110),Rgb(70,150,250),Rgb(180,100,230),Rgb(250,170,190)};
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(38, 38, 50), 1400U, 6U), p_(a) { (void)p_.Load("art", g_, kBytes); }
    void Tick(uint32_t ms) { if (dirty_) { idle_ += ms; if (idle_ > 900U) { p_.Save("art", g_, kBytes); dirty_ = false; } } Draw(); }
    void Press(int32_t x, int32_t y) { idle_ = 0U; if (x >= kC * kS) { const int32_t i = m::Clamp(y / 27, 0, 8); if (i == 8) { for (auto& v : g_) { v = 0x00U; } dirty_ = true; } else { col_ = i; } return; } Paint(x, y); }
    void Drag(int32_t x, int32_t y) { if (x < kC * kS) { Paint(x, y); } }
    void Button(KeyCode k) { if (k == kVolUp) { col_ = (col_ + 1) % 8; } else if (k == kVolDown) { col_ = (col_ + 7) % 8; } }

   private:
    int32_t At(int32_t x, int32_t y) const { const int32_t i = y * kC + x; return (g_[i / 2] >> ((i % 2) * 4)) & 0x0FU; }
    void Paint(int32_t x, int32_t y) { const int32_t cx = x / kS, cy = y / kS; if (cx < 0 || cy < 0 || cx >= kC || cy >= kR) { return; } const int32_t i = cy * kC + cx, sh = (i % 2) * 4; g_[i / 2] = static_cast<uint8_t>((g_[i / 2] & ~(0x0FU << sh)) | (static_cast<uint32_t>(col_) << sh)); dirty_ = true; idle_ = 0U; }
    void Draw() {
        c_.Begin();
        for (int32_t y = 0; y < kR; ++y) { int32_t s = 0; for (int32_t x = 1; x <= kC; ++x) { if (x == kC || At(x, y) != At(s, y)) { c_.Fill(s * kS, y * kS, (x - s) * kS, kS, kPal[At(s, y)]); s = x; } } }
        for (int32_t i = 0; i < 8; ++i) { const int32_t x = kC * kS + 4, y = i * 27 + 3; c_.Round(x, y, 62, 23, 4, kPal[i]); if (i == col_) { c_.Frame(x - 2, y - 2, 66, 27, kWhite, 2); } }
        c_.Text(kW - 36, 222, "Clear", kWhite, kSmall, true);
        c_.Present();
    }
    Canvas c_; Prefs p_; uint8_t g_[kBytes]{}; int32_t col_{1}; uint32_t idle_{}; bool dirty_{};
};
}  // namespace
PORTKIT_MAIN(Game)
