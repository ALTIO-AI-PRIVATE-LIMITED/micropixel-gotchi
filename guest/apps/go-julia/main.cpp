// SPDX-License-Identifier: Apache-2.0
//
// Julia Drift: a fractal that slowly changes shape. Drag to steer it, tap for new colours.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 37, kR = 30, kS = 8;
uint8_t v[kR][kC];
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(0, 0, 0), 1200U, 3U) {}
    void Tick(uint32_t) { if (!held_) { t_ += 0.006F; cr_ = -0.75F + 0.32F * m::Cos(t_); ci_ = 0.27F * m::Sin(t_ * 1.7F) + 0.05F; } for (int32_t i = 0; i < 6; ++i) { Row(row_); row_ = (row_ + 1) % kR; } Draw(); }
    void Press(int32_t x, int32_t y) { held_ = true; moved_ = false; Set(x, y); }
    void Drag(int32_t x, int32_t y) { moved_ = true; Set(x, y); }
    void Tap(int32_t, int32_t) { held_ = false; if (!moved_) { pal_ = (pal_ + 1) % 3; } }
    void Button(KeyCode k) { if (k == kBoot) { pal_ = (pal_ + 1) % 3; } }

   private:
    void Set(int32_t x, int32_t y) { cr_ = -1.2F + static_cast<float>(x) * 0.0067F; ci_ = -0.8F + static_cast<float>(y) * 0.0066F; }
    void Row(int32_t r) { for (int32_t c = 0; c < kC; ++c) { float zr = (static_cast<float>(c) - 18.5F) * 0.086F, zi = (static_cast<float>(r) - 15.0F) * 0.086F; int32_t i = 0; while (i < 28 && zr * zr + zi * zi < 4.0F) { const float t = zr * zr - zi * zi + cr_; zi = 2.0F * zr * zi + ci_; zr = t; ++i; } v[r][c] = static_cast<uint8_t>(i); } }
    Color Col(int32_t i) const { if (i >= 28) { return Rgb(0, 0, 0); } const auto t = static_cast<uint8_t>(i * 9); if (pal_ == 0) { return Rgb(t, static_cast<uint8_t>(t / 2), static_cast<uint8_t>(255 - t)); } if (pal_ == 1) { return Rgb(static_cast<uint8_t>(255 - t / 2), t, static_cast<uint8_t>(t / 3)); } return Rgb(static_cast<uint8_t>(t / 2), static_cast<uint8_t>(255 - t), t); }
    void Draw() { c_.Begin(); for (int32_t y = 0; y < kR; ++y) { int32_t s = 0; for (int32_t x = 1; x <= kC; ++x) { if (x == kC || v[y][x] != v[y][s]) { c_.Fill(s * kS, y * kS, (x - s) * kS, kS, Col(v[y][s])); s = x; } } } c_.Present(); }
    Canvas c_; float t_{}, cr_{-0.75F}, ci_{0.1F}; int32_t row_{}, pal_{}; bool held_{}, moved_{};
};
}  // namespace
PORTKIT_MAIN(Game)
