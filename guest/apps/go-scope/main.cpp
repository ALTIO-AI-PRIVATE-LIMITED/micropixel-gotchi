// SPDX-License-Identifier: Apache-2.0
//
// Scope: see the sound wave from the microphone. Tap to freeze, volume keys for gain.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kN = 512; int16_t pcm[kN]; int16_t view[kN];
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(6, 20, 14), 900U, 6U), mic_(a) { mic_.Start(); }
    void Mic() { const uint32_t n = mic_.Read(pcm + fill_, static_cast<uint32_t>(kN - fill_)); fill_ += static_cast<int32_t>(n); if (fill_ >= kN) { fill_ = 0; if (!freeze_) { for (int32_t i = 0; i < kN; ++i) { view[i] = pcm[i]; } } } }
    void Tick(uint32_t) { Draw(); }
    void Tap(int32_t x, int32_t y) { if (y > 210) { step_ = step_ == 1 ? 2 : (step_ == 2 ? 3 : 1); (void)x; } else { freeze_ = !freeze_; } }
    void Button(KeyCode k) { if (k == kBoot) { freeze_ = !freeze_; } else { gain_ = m::Clamp(gain_ + (k == kVolUp ? 1 : -1), 1, 8); } }

   private:
    void Draw() {
        c_.Begin(); for (int32_t x = 0; x <= kW; x += 37) { c_.Fill(x, 0, 1, 210, Rgb(16, 56, 38)); } for (int32_t y = 0; y <= 210; y += 35) { c_.Fill(0, y, kW, 1, Rgb(16, 56, 38)); }
        int32_t start = 0; for (int32_t i = 1; i < kN / 3; ++i) { if (view[i - 1] < 0 && view[i] >= 0) { start = i; break; } }
        int32_t py = 105; for (int32_t x = 0; x < kW; ++x) { const int32_t idx = start + x * step_; if (idx >= kN) { break; } const int32_t y = m::Clamp(105 - view[idx] * gain_ / 300, 2, 208); const int32_t a = m::Min(py, y), h = m::Max(m::Abs(py - y), 1); c_.Fill(x, a, 2, h + 1, Rgb(90, 255, 150)); py = y; }
        c_.Text(6, 216, freeze_ ? "Frozen" : "Live", Rgb(90, 255, 150)); c_.Text(90, 216, "Gain", kGrey); c_.Number(130, 216, gain_, kWhite); c_.Text(200, 216, "Time x", kGrey); c_.Number(250, 216, step_, kWhite);
        c_.Present();
    }
    Canvas c_; portkit::Mic mic_; int32_t fill_{}, step_{1}, gain_{2}; bool freeze_{};
};
}  // namespace
PORTKIT_MAIN(Game)
