// SPDX-License-Identifier: Apache-2.0
//
// Marble Run: tilt the Gotchi to roll a marble out of a new maze each level.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 13, kR = 10, kS = 22, kX0 = 5, kY0 = 2;
constexpr uint8_t kE = 1, kS2 = 2, kWl = 4, kN = 8;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(20, 16, 34)), t_(a), p_(a) { best_ = p_.Get("lvl", 1U); Build(); }
    void Tick(uint32_t ms) { t_.Poll(); clock_ += ms; if (win_ > 0) { if (--win_ == 0) { ++level_; if (static_cast<uint32_t>(level_) > best_) { best_ = static_cast<uint32_t>(level_); p_.Set("lvl", best_); } Build(); } } else { Step(); } Draw(); }
    void Press(int32_t x, int32_t y) { held_ = true; hx_ = x; hy_ = y; }
    void Drag(int32_t x, int32_t y) { hx_ = x; hy_ = y; }
    void Tap(int32_t, int32_t) { held_ = false; }
    void Button(KeyCode k) { if (k == kBoot) { Build(); } }

   private:
    void Build() {
        for (auto& r : w_) { for (auto& v : r) { v = kE | kS2 | kWl | kN; } }
        bool seen[kR][kC]{}; uint8_t sx[kC * kR], sy[kC * kR]; int32_t n = 1; sx[0] = 0U; sy[0] = 0U; seen[0][0] = true;
        constexpr int32_t dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1}; constexpr uint8_t bit[] = {kE, kS2, kWl, kN}, back[] = {kWl, kN, kE, kS2};
        while (n > 0) {
            const int32_t x = sx[n - 1], y = sy[n - 1]; int32_t open[4], c = 0;
            for (int32_t d = 0; d < 4; ++d) { const int32_t nx = x + dx[d], ny = y + dy[d]; if (nx >= 0 && ny >= 0 && nx < kC && ny < kR && !seen[ny][nx]) { open[c++] = d; } }
            if (c == 0) { --n; continue; }
            const int32_t d = open[a_.random().Below(static_cast<uint32_t>(c))], nx = x + dx[d], ny = y + dy[d];
            w_[y][x] &= static_cast<uint8_t>(~bit[d]); w_[ny][nx] &= static_cast<uint8_t>(~back[d]); seen[ny][nx] = true; sx[n] = static_cast<uint8_t>(nx); sy[n] = static_cast<uint8_t>(ny); ++n;
        }
        x_ = y_ = static_cast<float>(kS) / 2.0F; vx_ = vy_ = 0.0F; clock_ = 0U;
    }
    void Clamp() {
        constexpr float r = 5.0F, s = static_cast<float>(kS);
        for (int32_t it = 0; it < 2; ++it) {
            const int32_t cx = m::Clamp(static_cast<int32_t>(x_ / s), 0, kC - 1), cy = m::Clamp(static_cast<int32_t>(y_ / s), 0, kR - 1);
            const float l = static_cast<float>(cx) * s, t = static_cast<float>(cy) * s; const uint8_t v = w_[cy][cx];
            if ((v & kE) != 0U && x_ + r > l + s) { x_ = l + s - r; vx_ *= -0.3F; }
            if ((v & kWl) != 0U && x_ - r < l) { x_ = l + r; vx_ *= -0.3F; }
            if ((v & kS2) != 0U && y_ + r > t + s) { y_ = t + s - r; vy_ *= -0.3F; }
            if ((v & kN) != 0U && y_ - r < t) { y_ = t + r; vy_ *= -0.3F; }
        }
    }
    void Step() {
        float ax = t_.x(), ay = t_.y();
        if (held_) { ax = static_cast<float>(hx_ - (kX0 + static_cast<int32_t>(x_))) * 0.02F; ay = static_cast<float>(hy_ - (kY0 + static_cast<int32_t>(y_))) * 0.02F; ax = m::Clamp(ax, -1.0F, 1.0F); ay = m::Clamp(ay, -1.0F, 1.0F); }
        vx_ = (vx_ + ax * 0.32F) * 0.97F; vy_ = (vy_ + ay * 0.32F) * 0.97F; vx_ = m::Clamp(vx_, -3.6F, 3.6F); vy_ = m::Clamp(vy_, -3.6F, 3.6F);
        x_ += vx_; Clamp(); y_ += vy_; Clamp();
        if (static_cast<int32_t>(x_) / kS == kC - 1 && static_cast<int32_t>(y_) / kS == kR - 1) { win_ = 36; Beep(a_, 880U, 180U); }
    }
    void Draw() {
        c_.Begin();
        c_.Fill(kX0, kY0, kC * kS, kR * kS, Rgb(52, 40, 30));
        c_.Disc(kX0 + (kC - 1) * kS + kS / 2, kY0 + (kR - 1) * kS + kS / 2, 8, Rgb(8, 8, 12));
        for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { const int32_t px = kX0 + x * kS, py = kY0 + y * kS; if ((w_[y][x] & kN) != 0U) { c_.Fill(px - 1, py - 1, kS + 2, 3, Rgb(226, 190, 130)); } if ((w_[y][x] & kWl) != 0U) { c_.Fill(px - 1, py - 1, 3, kS + 2, Rgb(226, 190, 130)); } if (x == kC - 1 && (w_[y][x] & kE) != 0U) { c_.Fill(px + kS - 1, py - 1, 3, kS + 2, Rgb(226, 190, 130)); } if (y == kR - 1 && (w_[y][x] & kS2) != 0U) { c_.Fill(px - 1, py + kS - 1, kS + 2, 3, Rgb(226, 190, 130)); } } }
        c_.Disc(kX0 + static_cast<int32_t>(x_), kY0 + static_cast<int32_t>(y_), 5, Rgb(120, 200, 255)); c_.Disc(kX0 + static_cast<int32_t>(x_) - 2, kY0 + static_cast<int32_t>(y_) - 2, 2, kWhite);
        char line[28] = "Level "; Canvas::Format(line + 6, level_);
        c_.Text(10, 224, line, kGrey); c_.Text(kW - 92, 224, "tilt to roll", kDim);
        if (win_ > 0) { Banner(c_, "NICE ROLL", "Next maze coming", Rgb(120, 200, 255)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; Prefs p_; uint8_t w_[kR][kC]{}; float x_{}, y_{}, vx_{}, vy_{}; int32_t level_{1}, win_{}, hx_{}, hy_{}; uint32_t best_{}, clock_{}; bool held_{};
};
}  // namespace
PORTKIT_MAIN(Game)
