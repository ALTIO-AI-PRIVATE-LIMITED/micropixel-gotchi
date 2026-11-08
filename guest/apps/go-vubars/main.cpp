// SPDX-License-Identifier: Apache-2.0
//
// VU Bars: a needle meter and sixteen sound bars from the microphone.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kN = 512, kB = 16; int16_t pcm[kN];
constexpr float kHz[kB] = {120,180,260,360,500,700,950,1300,1700,2200,2900,3700,4600,5500,6500,7500};
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(10, 14, 24)), mic_(a) { mic_.Start(); for (int32_t i = 0; i < kB; ++i) { co_[i] = 2.0F * m::Cos(m::kTwoPi * kHz[i] / 16000.0F); } }
    void Mic() { const uint32_t n = mic_.Read(pcm + fill_, static_cast<uint32_t>(kN - fill_)); fill_ += static_cast<int32_t>(n); if (fill_ >= kN) { fill_ = 0; Update(); } }
    void Tick(uint32_t) { Draw(); }
    void Tap(int32_t, int32_t) { hue_ = (hue_ + 1) % 3; }
    void Button(KeyCode k) { if (k == kBoot) { hue_ = (hue_ + 1) % 3; } }

   private:
    void Update() {
        float sum = 0.0F; for (int32_t i = 0; i < kN; ++i) { const float v = static_cast<float>(pcm[i]) / 32768.0F; sum += v * v; } rms_ = m::Max(m::Sqrt(sum / kN), rms_ * 0.82F);
        for (int32_t b = 0; b < kB; ++b) { float s0 = 0.0F, s1 = 0.0F; for (int32_t i = 0; i < kN; i += 2) { const float s = static_cast<float>(pcm[i]) / 32768.0F + co_[b] * s1 - s0; s0 = s1; s1 = s; } const float p = m::Sqrt(m::Max(s1 * s1 + s0 * s0 - co_[b] * s0 * s1, 0.0F)) / 40.0F; v_[b] = m::Max(p, v_[b] * 0.82F); peak_ = m::Max(peak_, v_[b]); } peak_ = m::Max(peak_ * 0.995F, 0.02F);
    }
    void Draw() {
        c_.Begin();
        constexpr Color a[] = {Rgb(90,240,160), Rgb(255,190,90), Rgb(120,180,255)};
        const float x = m::Clamp(rms_ * 5.0F, 0.0F, 1.0F), ang = -2.4F + x * 1.7F;
        c_.Round(38, 8, 220, 92, 10, Rgb(240, 230, 200)); for (int32_t i = 0; i <= 10; ++i) { const float t = -2.4F + static_cast<float>(i) * 0.17F; c_.Fill(148 + static_cast<int32_t>(m::Cos(t) * 76.0F), 96 + static_cast<int32_t>(m::Sin(t) * 76.0F), 3, 3, i > 7 ? Rgb(210, 50, 50) : Rgb(40, 40, 50)); }
        c_.Line(148, 96, 148 + static_cast<int32_t>(m::Cos(ang) * 70.0F), 96 + static_cast<int32_t>(m::Sin(ang) * 70.0F), Rgb(180, 40, 40), 2);
        for (int32_t b = 0; b < kB; ++b) { const int32_t h = m::Clamp(static_cast<int32_t>(v_[b] / peak_ * 120.0F), 2, 124); c_.Fill(6 + b * 18, 236 - h, 14, h, Color::Mix(Rgb(255, 90, 90), a[hue_], static_cast<uint8_t>(m::Clamp(h * 2, 0, 255)))); }
        c_.Present();
    }
    Canvas c_; portkit::Mic mic_; float co_[kB]{}, v_[kB]{}, rms_{}, peak_{0.02F}; int32_t fill_{}, hue_{};
};
}  // namespace
PORTKIT_MAIN(Game)
