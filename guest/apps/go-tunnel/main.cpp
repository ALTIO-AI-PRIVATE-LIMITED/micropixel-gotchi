// SPDX-License-Identifier: Apache-2.0
//
// Tunnel Pilot: tilt to fly through the gates as the tunnel rushes at you.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct Gate { float z, x, y; };
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(4, 6, 20)), t_(a), p_(a) { best_ = p_.Get("best"); Reset(); }
    void Tick(uint32_t) { t_.Poll(); if (state_ == 1) { Step(); } Draw(); }
    void Press(int32_t x, int32_t y) { if (state_ != 1) { state_ = 1; Reset(); } else { tx_ = x; ty_ = y; held_ = true; } }
    void Drag(int32_t x, int32_t y) { tx_ = x; ty_ = y; held_ = true; }
    void Tap(int32_t, int32_t) { held_ = false; }
    void Button(KeyCode k) { if (state_ != 1) { state_ = 1; Reset(); } else if (k == kVolUp) { px_ += 18.0F; } else if (k == kVolDown) { px_ -= 18.0F; } }

   private:
    void Place(Gate& g, float z) { g.z = z; g.x = static_cast<float>(a_.random().Below(121U)) - 60.0F; g.y = static_cast<float>(a_.random().Below(81U)) - 40.0F; }
    void Reset() { px_ = py_ = 0.0F; speed_ = 0.05F; score_ = 0U; lives_ = 3; for (int32_t i = 0; i < 6; ++i) { Place(g_[i], 2.0F + static_cast<float>(i) * 1.6F); } }
    void Step() {
        float ax = t_.x(), ay = t_.y(); if (held_) { ax = static_cast<float>(tx_ - kW / 2) / 100.0F; ay = static_cast<float>(ty_ - kH / 2) / 80.0F; }
        px_ = m::Clamp(px_ + ax * 6.0F, -110.0F, 110.0F); py_ = m::Clamp(py_ + ay * 5.0F, -80.0F, 80.0F); speed_ += 0.00002F;
        float far = 0.0F; for (auto& g : g_) { far = m::Max(far, g.z); }
        for (auto& g : g_) { g.z -= speed_; if (g.z <= 0.3F) { if (m::Abs(px_ - g.x) < 46.0F && m::Abs(py_ - g.y) < 38.0F) { score_ += 1U; Beep(a_, 660U + score_ * 8U, 40U); } else { --lives_; flash_ = 8; Beep(a_, 130U, 200U, 260U, W::kNoise); if (lives_ <= 0) { state_ = 2; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } } } Place(g, far + 1.6F); far = g.z; } }
    }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 40; ++i) { const float z = 0.2F + static_cast<float>((i * 37) % 40) / 10.0F; c_.Fill(148 + static_cast<int32_t>(static_cast<float>((i * 53) % 200 - 100) / z), 120 + static_cast<int32_t>(static_cast<float>((i * 29) % 160 - 80) / z), 2, 2, kDim); }
        for (int32_t n = 0; n < 6; ++n) { int32_t best = -1; float bz = -1.0F; for (int32_t i = 0; i < 6; ++i) { if (g_[i].z > bz && (n == 0 || g_[i].z < last_)) { bz = g_[i].z; best = i; } } if (best < 0) { break; } last_ = bz; const Gate& g = g_[best]; const float s = 1.0F / g.z; const int32_t cx = 148 + static_cast<int32_t>((g.x - px_) * s), cy = 120 + static_cast<int32_t>((g.y - py_) * s), w = static_cast<int32_t>(96.0F * s), h = static_cast<int32_t>(78.0F * s); c_.Frame(cx - w / 2, cy - h / 2, w, h, Color::Mix(Rgb(120, 240, 255), Rgb(30, 40, 100), static_cast<uint8_t>(m::Clamp(255.0F * s, 40.0F, 255.0F))), m::Clamp(static_cast<int32_t>(6.0F * s), 1, 8)); }
        c_.Fill(148 - 16, 200, 32, 6, Rgb(255, 170, 90)); c_.Fill(148 - 4, 192, 8, 14, Rgb(255, 214, 140));
        c_.Number(10, 8, static_cast<int32_t>(score_), kWhite, kLarge); for (int32_t i = 0; i < lives_; ++i) { c_.Disc(kW - 14 - i * 16, 16, 5, Rgb(120, 230, 255)); }
        if (flash_ > 0) { --flash_; c_.Fill(0, 0, kW, kH, Rgb(255, 60, 60), 90U); }
        if (state_ == 0) { Banner(c_, "TUNNEL PILOT", "Tilt to steer, tap to fly", Rgb(120, 230, 255)); } if (state_ == 2) { Banner(c_, "HULL GONE", "Tap to fly again", Rgb(255, 130, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Prefs p_; Gate g_[6]{}; float px_{}, py_{}, speed_{}, last_{}; int32_t state_{}, lives_{3}, tx_{}, ty_{}, flash_{}; uint32_t score_{}, best_{}; bool held_{};
};
}  // namespace
PORTKIT_MAIN(Game)
