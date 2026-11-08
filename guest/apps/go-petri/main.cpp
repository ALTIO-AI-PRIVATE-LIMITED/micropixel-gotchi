// SPDX-License-Identifier: Apache-2.0
//
// Petri: paint cells and watch them live, die and spread. Tap to paint, BOOT to pause, volume to change speed or clear.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 74, kR = 56, kS = 4;
uint8_t g[kR][kC], nn[kR][kC];
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(8, 12, 18), 2600U, 4U) { Seed(); }
    void Tick(uint32_t ms) { el_ += ms; if (run_ && el_ >= kGap[speed_]) { el_ = 0U; Step(); } Draw(); }
    void Press(int32_t x, int32_t y) { Paint(x, y); }
    void Drag(int32_t x, int32_t y) { Paint(x, y); }
    void Button(KeyCode k) { if (k == kBoot) { run_ = !run_; } else if (k == kVolUp) { speed_ = (speed_ + 1) % 4; } else { Seed(); } }

   private:
    static constexpr uint32_t kGap[4] = {160U, 90U, 40U, 300U};
    void Seed() { for (auto& r : g) { for (auto& v : r) { v = a_.random().Below(4U) == 0U ? 1U : 0U; } } gen_ = 0; }
    void Paint(int32_t x, int32_t y) { const int32_t cx = x / kS, cy = (y - 16) / kS; for (int32_t dy = -1; dy <= 1; ++dy) { for (int32_t dx = -1; dx <= 1; ++dx) { const int32_t px = cx + dx, py = cy + dy; if (px >= 0 && py >= 0 && px < kC && py < kR) { g[py][px] = 1U; } } } }
    void Step() { for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { int32_t n = 0; for (int32_t dy = -1; dy <= 1; ++dy) { for (int32_t dx = -1; dx <= 1; ++dx) { if (dx != 0 || dy != 0) { n += g[(y + dy + kR) % kR][(x + dx + kC) % kC]; } } } nn[y][x] = static_cast<uint8_t>(n == 3 || (g[y][x] != 0U && n == 2)); } } for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { g[y][x] = nn[y][x]; } } ++gen_; }
    void Draw() {
        c_.Begin();
        for (int32_t y = 0; y < kR; ++y) { int32_t s = -1; for (int32_t x = 0; x <= kC; ++x) { const bool on = x < kC && g[y][x] != 0U; if (on && s < 0) { s = x; } if (!on && s >= 0) { c_.Fill(s * kS, 16 + y * kS, (x - s) * kS, kS, Color::Mix(Rgb(90, 230, 160), Rgb(70, 140, 255), static_cast<uint8_t>(y * 4))); s = -1; } } }
        c_.Text(6, 0, "GEN", kGrey); c_.Number(38, 0, gen_, kWhite); c_.Text(200, 0, run_ ? "running" : "paused", kGrey);
        c_.Present();
    }
    Application& a_; Canvas c_; uint32_t el_{}; int32_t speed_{}, gen_{}; bool run_{true};
};
}  // namespace
PORTKIT_MAIN(Game)
