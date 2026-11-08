// SPDX-License-Identifier: Apache-2.0
//
// Treasure Radar: a sweeping radar prop with blips and a hot and cold game. It does not detect anything real.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct Blip { float x, y; int32_t age; };
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(4, 18, 10), 1700U, 8U), t_(a) { Hide(); }
    void Tick(uint32_t) { t_.Poll(); ang_ += 0.06F + t_.x() * 0.03F; if (ang_ > m::kTwoPi) { ang_ -= m::kTwoPi; if (a_.random().Below(2U) == 0U) { Add(); } } for (auto& b : b_) { if (b.age > 0) { --b.age; } } Draw(); }
    void Press(int32_t x, int32_t y) { const int32_t dx = x - tx_, dy = y - ty_, d = dx * dx + dy * dy; hit_ = d < 900; if (hit_) { ++found_; Beep(a_, 1046U, 260U); Hide(); } else { Beep(a_, d < 6400 ? 900U : (d < 20000 ? 560U : 260U), 90U, 200U, W::kTriangle); msg_ = d < 6400 ? "Hot" : (d < 20000 ? "Warm" : "Cold"); } }
    void Button(KeyCode k) { if (k == kBoot) { Hide(); } }

   private:
    void Hide() { const float a = static_cast<float>(a_.random().Below(628U)) * 0.01F, r = 30.0F + static_cast<float>(a_.random().Below(70U)); tx_ = 148 + static_cast<int32_t>(m::Cos(a) * r); ty_ = 120 + static_cast<int32_t>(m::Sin(a) * r); msg_ = "Tap to search"; }
    void Add() { for (auto& b : b_) { if (b.age == 0) { const float r = 20.0F + static_cast<float>(a_.random().Below(90U)); b = {m::Cos(ang_) * r, m::Sin(ang_) * r, 120}; Beep(a_, 1400U, 22U, 110U); break; } } }
    void Draw() {
        c_.Begin(); const Color g = Rgb(70, 255, 130);
        for (int32_t r = 30; r <= 110; r += 40) { c_.Ring(148, 120, r, Rgb(30, 110, 60)); } c_.Fill(148, 8, 1, 224, Rgb(20, 80, 40)); c_.Fill(36, 120, 224, 1, Rgb(20, 80, 40));
        for (int32_t i = 0; i < 8; ++i) { const float t = ang_ - static_cast<float>(i) * 0.07F; const uint8_t op = static_cast<uint8_t>(190 - i * 22); for (int32_t r = 6; r < 112; r += 8) { c_.Fill(148 + static_cast<int32_t>(m::Cos(t) * static_cast<float>(r)), 120 + static_cast<int32_t>(m::Sin(t) * static_cast<float>(r)), 3, 3, g, op); } }
        for (const auto& b : b_) { if (b.age > 0) { c_.Disc(148 + static_cast<int32_t>(b.x), 120 + static_cast<int32_t>(b.y), 4, g, static_cast<uint8_t>(m::Min(b.age * 4, 255))); } }
        c_.Text(6, 6, msg_, g); c_.Text(6, 216, "Found", g); c_.Number(52, 216, found_, kWhite);
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Blip b_[10]{}; float ang_{}; int32_t tx_{}, ty_{}, found_{}; bool hit_{}; const char* msg_{""};
};
}  // namespace
PORTKIT_MAIN(Game)
