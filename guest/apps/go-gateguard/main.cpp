// SPDX-License-Identifier: Apache-2.0
//
// Gate Guard: build towers along the road and stop the waves. Tap a pad to build or upgrade.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kWay[][2] = {{-14,60},{70,60},{70,160},{170,160},{170,60},{262,60},{262,190},{310,190}}; constexpr int32_t kWN = 8;
struct Pad { int32_t x, y, kind, lvl; }; struct En { float x, y; int32_t leg, hp, max; float speed; bool on; };
const char* const kType[] = {"Bolt","Frost","Bomb"}; constexpr Color kKc[] = {Rgb(255,214,90),Rgb(120,220,255),Rgb(255,120,90)}; constexpr int32_t kCost[] = {40,60,90};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(46, 90, 60)), p_(a) { best_ = p_.Get("best"); New(); }
    void Tick(uint32_t) { ++f_; if (s_ == 1) { Step(); } Draw(); }
    void Press(int32_t x, int32_t y) {
        if (s_ == 2) { New(); return; }
        if (y > 212) { if (x < 200) { type_ = m::Clamp(x / 66, 0, 2); } else { Start(); } return; }
        for (auto& p : pad_) { if (m::Abs(x - p.x) < 14 && m::Abs(y - p.y) < 14) { Build(p); return; } }
    }
    void Button(KeyCode k) { if (k == kBoot) { Start(); } else { type_ = (type_ + 1) % 3; } }

   private:
    void New() { const int32_t ps[10][2] = {{36,92},{106,92},{36,130},{106,130},{136,190},{206,92},{206,130},{136,110},{226,190},{290,120}}; for (int32_t i = 0; i < 10; ++i) { pad_[i] = {ps[i][0], ps[i][1], -1, 0}; } for (auto& e : e_) { e.on = false; } gold_ = 120; lives_ = 12; wave_ = 0; s_ = 0; }
    void Start() { if (s_ != 0) { return; } ++wave_; todo_ = 6 + wave_ * 2; gap_ = 0; s_ = 1; }
    void Build(Pad& p) { if (p.kind < 0) { if (gold_ >= kCost[type_]) { gold_ -= kCost[type_]; p.kind = type_; p.lvl = 1; Beep(a_, 620U, 50U); } } else if (p.lvl < 3 && gold_ >= kCost[p.kind] * p.lvl) { gold_ -= kCost[p.kind] * p.lvl; ++p.lvl; Beep(a_, 820U, 50U); } }
    void Step() {
        if (todo_ > 0 && --gap_ <= 0) { for (auto& e : e_) { if (!e.on) { const int32_t hp = 8 + wave_ * 5; e = {static_cast<float>(kWay[0][0]), static_cast<float>(kWay[0][1]), 1, hp, hp, 1.0F + static_cast<float>(wave_) * 0.05F, true}; --todo_; gap_ = 22; break; } } }
        bool any = todo_ > 0;
        for (auto& e : e_) { if (!e.on) { continue; } any = true; const float tx = static_cast<float>(kWay[e.leg][0]), ty = static_cast<float>(kWay[e.leg][1]), dx = tx - e.x, dy = ty - e.y, d = m::Sqrt(dx * dx + dy * dy); float sp = e.speed; if (slow_[&e - e_] > 0) { sp *= 0.5F; --slow_[&e - e_]; } if (d <= sp) { e.x = tx; e.y = ty; if (++e.leg >= kWN) { e.on = false; if (--lives_ <= 0) { s_ = 2; if (static_cast<uint32_t>(wave_) > best_) { best_ = static_cast<uint32_t>(wave_); p_.Set("best", best_); } Beep(a_, 110U, 500U); return; } } } else { e.x += dx / d * sp; e.y += dy / d * sp; } }
        if (f_ % 15 == 0) { for (int32_t i = 0; i < 10; ++i) { Fire(i); } }
        if (fx_ > 0) { --fx_; }
        if (!any) { s_ = 0; gold_ += 30 + wave_ * 5; Beep(a_, 880U, 200U); }
    }
    void Fire(int32_t i) { Pad& p = pad_[i]; if (p.kind < 0) { return; } const float range = 56.0F + static_cast<float>(p.lvl) * 12.0F; En* best = nullptr; float top = -1.0F; for (auto& e : e_) { if (!e.on) { continue; } const float dx = e.x - static_cast<float>(p.x), dy = e.y - static_cast<float>(p.y); if (dx * dx + dy * dy < range * range && static_cast<float>(e.leg) + e.x * 0.001F > top) { top = static_cast<float>(e.leg) + e.x * 0.001F; best = &e; } } if (best == nullptr) { return; } lx_[i] = static_cast<int32_t>(best->x); ly_[i] = static_cast<int32_t>(best->y); lt_[i] = 4; const int32_t dmg = (p.kind == 0 ? 5 : (p.kind == 1 ? 2 : 9)) * p.lvl; if (p.kind == 2) { for (auto& e : e_) { if (e.on && m::Abs(e.x - best->x) < 26.0F && m::Abs(e.y - best->y) < 26.0F) { Hurt(e, dmg); } } } else { Hurt(*best, dmg); if (p.kind == 1) { slow_[best - e_] = 60; } } }
    void Hurt(En& e, int32_t dmg) { e.hp -= dmg; if (e.hp <= 0 && e.on) { e.on = false; gold_ += 5 + wave_; Beep(a_, 300U, 18U, 90U, W::kNoise); } }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i + 1 < kWN; ++i) { const int32_t x0 = m::Min(kWay[i][0], kWay[i + 1][0]), y0 = m::Min(kWay[i][1], kWay[i + 1][1]), x1 = m::Max(kWay[i][0], kWay[i + 1][0]), y1 = m::Max(kWay[i][1], kWay[i + 1][1]); c_.Fill(x0 - 10, y0 - 10, x1 - x0 + 20, y1 - y0 + 20, Rgb(170, 140, 100)); }
        for (const auto& p : pad_) { c_.Round(p.x - 12, p.y - 12, 24, 24, 5, Rgb(70, 110, 76)); if (p.kind >= 0) { c_.Disc(p.x, p.y, 6 + p.lvl * 2, kKc[p.kind]); } else { c_.Frame(p.x - 5, p.y - 5, 10, 10, Rgb(120, 170, 120)); } }
        for (int32_t i = 0; i < 10; ++i) { if (lt_[i] > 0) { --lt_[i]; c_.Line(pad_[i].x, pad_[i].y, lx_[i], ly_[i], kKc[pad_[i].kind], 1); } }
        for (const auto& e : e_) { if (e.on) { const auto x = static_cast<int32_t>(e.x), y = static_cast<int32_t>(e.y); c_.Disc(x, y, 7, slow_[&e - e_] > 0 ? Rgb(150, 210, 255) : Rgb(200, 60, 90)); c_.Fill(x - 8, y - 12, e.hp * 16 / e.max, 2, Rgb(120, 240, 140)); } }
        c_.Fill(0, 0, kW, 22, Rgb(24, 40, 30)); c_.Text(8, 3, "Gold", Rgb(255, 214, 110)); c_.Number(46, 3, gold_, kWhite); c_.Text(104, 3, "Lives", Rgb(255, 130, 130)); c_.Number(146, 3, lives_, kWhite); c_.Text(200, 3, "Wave", kGrey); c_.Number(240, 3, wave_, kWhite);
        for (int32_t i = 0; i < 3; ++i) { c_.Round(2 + i * 66, 214, 62, 24, 5, i == type_ ? kKc[i].Darkened(60U) : Rgb(30, 50, 40)); c_.Text(33 + i * 66, 218, kType[i], kWhite, kSmall, true); }
        c_.Round(204, 214, 90, 24, 5, s_ == 0 ? Rgb(80, 160, 100) : Rgb(50, 60, 50)); c_.Text(249, 218, s_ == 0 ? "Next wave" : "Battle", kWhite, kSmall, true);
        if (s_ == 2) { Banner(c_, "GATE FELL", "Tap to rebuild", Rgb(255, 130, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; Pad pad_[10]{}; En e_[24]{}; int32_t slow_[24]{}, lx_[10]{}, ly_[10]{}, lt_[10]{}, f_{}, s_{}, gold_{}, lives_{}, wave_{}, todo_{}, gap_{}, type_{}, fx_{}; uint32_t best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
