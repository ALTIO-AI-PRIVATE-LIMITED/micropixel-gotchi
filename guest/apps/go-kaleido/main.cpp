// SPDX-License-Identifier: Apache-2.0
//
// Kaleido: drifting shapes mirrored twelve ways. Drag to stir, tap for new colours, BOOT to pause.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kN = 34, kFold = 6;
constexpr Color kPal[6][3] = {{Rgb(255,90,120),Rgb(255,200,80),Rgb(90,220,255)},{Rgb(120,255,170),Rgb(60,140,255),Rgb(240,240,120)},{Rgb(190,100,255),Rgb(255,120,200),Rgb(120,250,240)},{Rgb(255,150,60),Rgb(255,70,70),Rgb(255,240,180)},{Rgb(90,255,110),Rgb(255,255,255),Rgb(30,180,120)},{Rgb(120,130,255),Rgb(255,110,255),Rgb(255,220,120)}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(4, 4, 12), 1300U, 2U) { for (auto& p : p_) { Seed(p); } }
    void Tick(uint32_t) {
        if (!paused_) { spin_ += 0.008F + stir_; stir_ *= 0.96F; for (auto& p : p_) { p.r += p.vr; p.a += p.va; if (p.r < 6.0F || p.r > 116.0F) { p.vr = -p.vr; p.r += p.vr; } } }
        c_.Begin();
        for (const auto& p : p_) {
            const Color k = kPal[pal_][p.k];
            for (int32_t f = 0; f < kFold; ++f) {
                const float base = spin_ + static_cast<float>(f) * m::kTwoPi / static_cast<float>(kFold);
                for (int32_t s = -1; s <= 1; s += 2) { const float t = base + p.a * static_cast<float>(s); c_.Fill(148 + static_cast<int32_t>(m::Cos(t) * p.r) - p.z / 2, 120 + static_cast<int32_t>(m::Sin(t) * p.r) - p.z / 2, p.z, p.z, k, 210U); }
            }
        }
        c_.Present();
    }
    void Drag(int32_t x, int32_t) { stir_ = static_cast<float>(x - lx_) * 0.002F; lx_ = x; }
    void Press(int32_t x, int32_t) { lx_ = x; moved_ = 0; }
    void Tap(int32_t, int32_t) { pal_ = (pal_ + 1) % 6; for (auto& p : p_) { if (a_.random().Below(3U) == 0U) { Seed(p); } } }
    void Button(KeyCode k) { if (k == kBoot) { paused_ = !paused_; } else { pal_ = (pal_ + 1) % 6; } }

   private:
    struct P { float r, a, vr, va; int32_t z; uint8_t k; };
    void Seed(P& p) { p.r = static_cast<float>(6U + a_.random().Below(110U)); p.a = static_cast<float>(a_.random().Below(52U)) * 0.01F; p.vr = (static_cast<float>(a_.random().Below(100U)) - 50.0F) * 0.012F + 0.05F; p.va = (static_cast<float>(a_.random().Below(100U)) - 50.0F) * 0.0002F; p.z = 3 + static_cast<int32_t>(a_.random().Below(11U)); p.k = static_cast<uint8_t>(a_.random().Below(3U)); }
    Application& a_; Canvas c_; P p_[kN]{}; float spin_{}, stir_{}; int32_t pal_{}, lx_{}, moved_{}; bool paused_{};
};
}  // namespace
PORTKIT_MAIN(Game)
