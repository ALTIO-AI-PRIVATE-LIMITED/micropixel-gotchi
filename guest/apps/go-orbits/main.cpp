// SPDX-License-Identifier: Apache-2.0
//
// Gravity Garden: flick planets around a star and watch them orbit, slingshot and crash.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct B { float x, y, vx, vy; bool on; int32_t k; }; constexpr int32_t kN = 12; constexpr Color kK[] = {Rgb(120,200,255),Rgb(255,170,90),Rgb(150,240,150),Rgb(255,120,180),Rgb(220,200,255)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(4, 6, 18)) { for (int32_t i = 0; i < 4; ++i) { Spawn(148.0F + static_cast<float>(40 + i * 20), 120.0F, 0.0F, 2.6F - static_cast<float>(i) * 0.3F); } }
    void Tick(uint32_t) { for (int32_t s = 0; s < 2; ++s) { Step(); } ++f_; Draw(); }
    void Press(int32_t x, int32_t y) { px_ = x; py_ = y; cx_ = x; cy_ = y; aim_ = true; }
    void Drag(int32_t x, int32_t y) { cx_ = x; cy_ = y; }
    void Tap(int32_t, int32_t) { if (aim_) { aim_ = false; Spawn(static_cast<float>(px_), static_cast<float>(py_), static_cast<float>(px_ - cx_) * 0.05F, static_cast<float>(py_ - cy_) * 0.05F); Beep(a_, 500U, 40U); } }
    void Button(KeyCode k) { if (k == kBoot) { for (auto& b : b_) { b.on = false; } } }

   private:
    void Spawn(float x, float y, float vx, float vy) { for (auto& b : b_) { if (!b.on) { b = {x, y, vx, vy, true, static_cast<int32_t>(a_.random().Below(5U))}; for (auto& t : tr_[&b - b_]) { t[0] = static_cast<int16_t>(x); t[1] = static_cast<int16_t>(y); } return; } } }
    void Step() { for (auto& b : b_) { if (!b.on) { continue; } const float dx = 148.0F - b.x, dy = 120.0F - b.y, d2 = dx * dx + dy * dy, d = m::Sqrt(d2); if (d < 18.0F) { b.on = false; Beep(a_, 120U, 120U, 200U, W::kNoise); continue; } const float g = 300.0F / d2; b.vx += g * dx / d; b.vy += g * dy / d; b.x += b.vx; b.y += b.vy; if (b.x < -200.0F || b.x > 500.0F || b.y < -200.0F || b.y > 440.0F) { b.on = false; } } }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 26; ++i) { c_.Fill((i * 83) % kW, (i * 47) % kH, 1, 1, kGrey); }
        c_.Disc(148, 120, 16, Rgb(255, 214, 110)); c_.Disc(148, 120, 22, Rgb(255, 170, 60), 60U);
        for (int32_t i = 0; i < kN; ++i) { if (!b_[i].on) { continue; } if (f_ % 2 == 0) { tr_[i][head_[i]][0] = static_cast<int16_t>(b_[i].x); tr_[i][head_[i]][1] = static_cast<int16_t>(b_[i].y); head_[i] = (head_[i] + 1) % 16; } for (const auto& t : tr_[i]) { c_.Fill(t[0], t[1], 2, 2, kK[b_[i].k], 110U); } c_.Disc(static_cast<int32_t>(b_[i].x), static_cast<int32_t>(b_[i].y), 5, kK[b_[i].k]); }
        if (aim_) { c_.Line(px_, py_, cx_, cy_, kWhite, 2); }
        c_.Text(8, 6, "Drag back and let go to launch", kGrey);
        c_.Present();
    }
    Application& a_; Canvas c_; B b_[kN]{}; int16_t tr_[kN][16][2]{}; int32_t head_[kN]{}, f_{}, px_{}, py_{}, cx_{}, cy_{}; bool aim_{};
};
}  // namespace
PORTKIT_MAIN(Game)
