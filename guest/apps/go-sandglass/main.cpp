// SPDX-License-Identifier: Apache-2.0
//
// Sand Glass: real falling grains inside a glass. Turn the Gotchi over to run it again.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 60, kR = 56, kS = 4, kX0 = 28, kY0 = 8; uint8_t g[kR][kC];
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 22, 40), 1500U, 4U), t_(a) { Fill(); }
    void Tick(uint32_t) { t_.Poll(); for (int32_t s = 0; s < 2; ++s) { Step(); } Draw(); }
    void Tap(int32_t, int32_t) { Fill(); }
    void Button(KeyCode k) { if (k == kBoot) { Fill(); } }

   private:
    static int32_t Half(int32_t y) { const int32_t d = y < kR / 2 ? kR / 2 - 1 - y : y - kR / 2; return d >= 25 ? 26 : 1 + d * 25 / 25; }
    static bool In(int32_t x, int32_t y) { return y >= 0 && y < kR && x >= kC / 2 - Half(y) && x < kC / 2 + Half(y); }
    void Fill() { for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { g[y][x] = In(x, y) && y < kR / 2 - 3 && y > 2 ? 1U : 0U; } } }
    void Step() {
        const bool up = t_.y() < -0.15F; const int32_t dy = up ? -1 : 1; const int32_t lean = t_.x() > 0.2F ? 1 : (t_.x() < -0.2F ? -1 : 0);
        const int32_t y0 = up ? 0 : kR - 1, y1 = up ? kR : -1, sy = up ? 1 : -1;
        ++tick_;
        for (int32_t y = y0; y != y1; y += sy) { for (int32_t ix = 0; ix < kC; ++ix) { const int32_t x = tick_ % 2 == 0 ? ix : kC - 1 - ix; if (g[y][x] == 0U) { continue; } const int32_t ny = y + dy; if (ny < 0 || ny >= kR) { continue; } const int32_t pref = lean != 0 ? lean : (a_.random().Below(2U) == 0U ? 1 : -1); const int32_t tries[3] = {0, pref, -pref}; for (int32_t d : tries) { const int32_t nx = x + d; if (nx >= 0 && nx < kC && In(nx, ny) && g[ny][nx] == 0U) { g[y][x] = 0U; g[ny][nx] = 1U; break; } } } }
    }
    void Draw() {
        c_.Begin();
        for (int32_t y = 0; y < kR; ++y) { const int32_t h = Half(y); c_.Fill(kX0 + (kC / 2 - h) * kS, kY0 + y * kS, h * 2 * kS, kS, Rgb(36, 44, 74)); int32_t s = -1; for (int32_t x = 0; x <= kC; ++x) { const bool on = x < kC && g[y][x] != 0U; if (on && s < 0) { s = x; } if (!on && s >= 0) { c_.Fill(kX0 + s * kS, kY0 + y * kS, (x - s) * kS, kS, Rgb(250, 200, 110)); s = -1; } } }
        c_.Fill(kX0, kY0 - 4, kC * kS, 4, Rgb(190, 150, 100)); c_.Fill(kX0, kY0 + kR * kS, kC * kS, 4, Rgb(190, 150, 100));
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; int32_t tick_{};
};
}  // namespace
PORTKIT_MAIN(Game)
