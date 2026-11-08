// SPDX-License-Identifier: Apache-2.0
//
// Star Patrol: drag or tilt your ship and clear the waves. It fires by itself.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct E { float x, y; float t; bool on; int8_t kind; }; struct S { float x, y; bool on; };
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(6, 8, 22)), t_(a), p_(a) { best_ = p_.Get("best"); }
    void Tick(uint32_t) { t_.Poll(); ++f_; if (state_ == 1) { Step(); } Draw(); }
    void Press(int32_t x, int32_t) { if (state_ != 1) { Start(); } else { x_ = static_cast<float>(x); drag_ = true; } }
    void Drag(int32_t x, int32_t) { x_ = static_cast<float>(x); drag_ = true; }
    void Tap(int32_t, int32_t) { drag_ = false; }
    void Button(KeyCode k) { if (state_ != 1) { Start(); } else if (k == kVolUp) { x_ += 24.0F; } else if (k == kVolDown) { x_ -= 24.0F; } }

   private:
    void Start() { for (auto& e : e_) { e.on = false; } for (auto& s : s_) { s.on = false; } for (auto& s : b_) { s.on = false; } x_ = 148.0F; lives_ = 3; score_ = 0U; wave_ = 0; state_ = 1; Wave(); }
    void Wave() { ++wave_; int32_t i = 0; for (int32_t y = 0; y < 3; ++y) { for (int32_t x = 0; x < 6; ++x) { e_[i++] = {46.0F + static_cast<float>(x) * 40.0F, 24.0F + static_cast<float>(y) * 28.0F - 90.0F, static_cast<float>(x) * 0.5F, true, static_cast<int8_t>(y)}; } } dir_ = 1.0F; }
    void Step() {
        if (!drag_) { x_ += t_.x() * 8.0F; } x_ = m::Clamp(x_, 14.0F, 282.0F);
        if (f_ % 8 == 0) { for (auto& s : s_) { if (!s.on) { s = {x_, 200.0F, true}; Beep(a_, 1100U, 10U, 50U); break; } } }
        for (auto& s : s_) { if (s.on) { s.y -= 8.0F; if (s.y < -6.0F) { s.on = false; } } }
        bool any = false, edge = false; const float speed = 0.6F + static_cast<float>(wave_) * 0.12F;
        for (auto& e : e_) { if (!e.on) { continue; } any = true; e.t += 0.05F; e.x += dir_ * speed; if (e.y < 24.0F) { e.y += 2.0F; } if (e.x < 14.0F || e.x > 282.0F) { edge = true; } }
        if (edge) { dir_ = -dir_; for (auto& e : e_) { if (e.on) { e.y += 7.0F; } } }
        if (!any) { Wave(); Beep(a_, 880U, 200U); return; }
        for (auto& e : e_) { if (!e.on) { continue; } if (a_.random().Below(400U) == 0U) { for (auto& b : b_) { if (!b.on) { b = {e.x, e.y, true}; break; } } } for (auto& s : s_) { if (s.on && m::Abs(s.x - e.x) < 14.0F && m::Abs(s.y - e.y) < 12.0F) { s.on = false; e.on = false; score_ += 10U * wave_; Beep(a_, 300U, 40U, 180U, W::kNoise); break; } } if (e.on && e.y > 206.0F) { e.on = false; Hurt(); } }
        for (auto& b : b_) { if (b.on) { b.y += 4.0F; if (b.y > 244.0F) { b.on = false; } else if (m::Abs(b.x - x_) < 14.0F && m::Abs(b.y - 214.0F) < 12.0F) { b.on = false; Hurt(); } } }
    }
    void Hurt() { if (--lives_ <= 0 && state_ == 1) { state_ = 2; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } Beep(a_, 100U, 400U, 280U, W::kNoise); } }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 30; ++i) { c_.Fill((i * 83) % kW, ((i * 61) + f_ * (1 + i % 3)) % kH, 2, 2, kDim); }
        constexpr Color k[] = {Rgb(255,110,130),Rgb(255,200,90),Rgb(120,220,150)};
        for (const auto& e : e_) { if (e.on) { const auto x = static_cast<int32_t>(e.x), y = static_cast<int32_t>(e.y) + static_cast<int32_t>(m::Sin(e.t) * 3.0F); c_.Fill(x - 11, y - 4, 22, 8, k[e.kind]); c_.Fill(x - 5, y - 9, 10, 18, k[e.kind]); c_.Fill(x - 4, y - 2, 3, 3, Rgb(10, 10, 20)); c_.Fill(x + 1, y - 2, 3, 3, Rgb(10, 10, 20)); } }
        for (const auto& s : s_) { if (s.on) { c_.Fill(static_cast<int32_t>(s.x) - 1, static_cast<int32_t>(s.y), 3, 9, Rgb(150, 240, 255)); } }
        for (const auto& s : b_) { if (s.on) { c_.Fill(static_cast<int32_t>(s.x) - 2, static_cast<int32_t>(s.y), 4, 8, Rgb(255, 120, 90)); } }
        const auto x = static_cast<int32_t>(x_); c_.Fill(x - 14, 218, 28, 8, Rgb(190, 210, 255)); c_.Fill(x - 4, 206, 8, 14, kWhite); c_.Fill(x - 2, 226, 4, 5 + f_ % 3, Rgb(255, 170, 70));
        c_.Number(8, 4, static_cast<int32_t>(score_), kWhite, kMedium); for (int32_t i = 0; i < lives_; ++i) { c_.Fill(kW - 18 - i * 16, 8, 12, 6, Rgb(190, 210, 255)); }
        if (state_ == 0) { Banner(c_, "STAR PATROL", "Tap to launch", Rgb(150, 240, 255)); } if (state_ == 2) { Banner(c_, "SHIP LOST", "Tap to try again", Rgb(255, 120, 90)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Prefs p_; E e_[18]{}; S s_[8]{}, b_[8]{}; float x_{148.0F}, dir_{1.0F}; int32_t f_{}, state_{}, lives_{3}; uint32_t wave_{}, score_{}, best_{}; bool drag_{};
};
}  // namespace
PORTKIT_MAIN(Game)
