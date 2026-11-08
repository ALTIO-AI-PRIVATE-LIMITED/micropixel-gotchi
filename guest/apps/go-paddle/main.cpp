// SPDX-License-Identifier: Apache-2.0
//
// Paddle Duel: first to seven. Two players on one Gotchi, or play the computer.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kPH = 46;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(10, 18, 32)) {}
    void Tick(uint32_t) { if (state_ == 1) { Step(); } Draw(); }
    void Touch(const micropixel::TouchEvent& e) {
        const int32_t x = e.x(), y = e.y();
        if (e.phase() == micropixel::TouchPhase::kDown && state_ != 1) { two_ = x > kW / 2; Start(); return; }
        if (e.phase() == micropixel::TouchPhase::kCancel || e.phase() == micropixel::TouchPhase::kUp) { return; }
        if (x < kW / 2) { l_ = static_cast<float>(y); } else if (two_) { r_ = static_cast<float>(y); } else { l_ = static_cast<float>(y); }
    }
    void Button(KeyCode k) { if (state_ != 1) { two_ = false; Start(); } else if (k == kVolUp) { l_ -= 22.0F; } else if (k == kVolDown) { l_ += 22.0F; } }

   private:
    void Start() { sl_ = sr_ = 0; state_ = 1; Serve(1.0F); }
    void Serve(float dir) { x_ = 148.0F; y_ = 120.0F; vx_ = 3.2F * dir; vy_ = static_cast<float>(a_.random().Below(5U)) - 2.0F; wait_ = 24; }
    void Step() {
        if (wait_ > 0) { --wait_; return; }
        if (!two_) { const float s = 3.0F + static_cast<float>(sl_) * 0.2F; r_ += m::Clamp(y_ - r_, -s, s); }
        l_ = m::Clamp(l_, 23.0F, 217.0F); r_ = m::Clamp(r_, 23.0F, 217.0F);
        x_ += vx_; y_ += vy_; if (y_ < 4.0F || y_ > 236.0F) { vy_ = -vy_; y_ += vy_; Beep(a_, 360U, 16U, 90U); }
        if (vx_ < 0.0F && x_ < 16.0F && x_ > 6.0F && m::Abs(y_ - l_) < 28.0F) { vx_ = m::Min(-vx_ * 1.06F, 8.0F); vy_ = (y_ - l_) * 0.16F; Beep(a_, 520U, 22U); }
        if (vx_ > 0.0F && x_ > 280.0F && x_ < 290.0F && m::Abs(y_ - r_) < 28.0F) { vx_ = -m::Min(vx_ * 1.06F, 8.0F); vy_ = (y_ - r_) * 0.16F; Beep(a_, 620U, 22U); }
        if (x_ < -8.0F) { ++sr_; Score(-1.0F); } if (x_ > 304.0F) { ++sl_; Score(1.0F); }
    }
    void Score(float dir) { Beep(a_, 220U, 140U); if (sl_ >= 7 || sr_ >= 7) { state_ = 2; } else { Serve(dir); } }
    void Draw() {
        c_.Begin();
        for (int32_t y = 6; y < kH; y += 18) { c_.Fill(147, y, 2, 10, kDim); }
        c_.Number(100, 8, sl_, kWhite, kTitle, true); c_.Number(196, 8, sr_, kWhite, kTitle, true);
        c_.Round(6, static_cast<int32_t>(l_) - kPH / 2, 8, kPH, 3, Rgb(110, 230, 255)); c_.Round(282, static_cast<int32_t>(r_) - kPH / 2, 8, kPH, 3, Rgb(255, 150, 110));
        c_.Disc(static_cast<int32_t>(x_), static_cast<int32_t>(y_), 5, kWhite);
        if (state_ == 0) { Banner(c_, "PADDLE DUEL", "Left half: vs computer   Right half: 2P", Rgb(110, 230, 255)); }
        if (state_ == 2) { Banner(c_, sl_ > sr_ ? "LEFT WINS" : "RIGHT WINS", "Left half: vs computer   Right half: 2P", Rgb(255, 150, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; float l_{120.0F}, r_{120.0F}, x_{148.0F}, y_{120.0F}, vx_{}, vy_{}; int32_t sl_{}, sr_{}, state_{}, wait_{}; bool two_{};
};
}  // namespace
PORTKIT_MAIN(Game)
