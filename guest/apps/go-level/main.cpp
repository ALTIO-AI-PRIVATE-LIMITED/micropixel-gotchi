// SPDX-License-Identifier: Apache-2.0
//
// Level Pro: a bubble level, an edge angle gauge and a motion meter. Tap to switch, BOOT to set zero.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 22, 20)), t_(a) {}
    void Tick(uint32_t) { t_.Poll(); peak_ = m::Max(peak_ * 0.995F, t_.magnitude()); if (t_.jolt() && cool_ == 0) { ++shakes_; cool_ = 12; } if (cool_ > 0) { --cool_; } Draw(); }
    void Tap(int32_t, int32_t) { mode_ = (mode_ + 1) % 3; Beep(a_, 700U, 30U); }
    void Button(KeyCode k) { if (k == kBoot) { zx_ = t_.x(); zy_ = t_.y(); peak_ = 0.0F; shakes_ = 0; } else { mode_ = (mode_ + 1) % 3; } }

   private:
    static int32_t Deg(float v) { const float q = m::Clamp(v, -0.99F, 0.99F); return static_cast<int32_t>(m::Atan(q / m::Sqrt(1.0F - q * q)) * 57.3F); }
    void Draw() {
        c_.Begin();
        const float x = t_.x() - zx_, y = t_.y() - zy_;
        if (mode_ == 0) {
            c_.Disc(148, 112, 96, Rgb(150, 210, 90)); c_.Ring(148, 112, 96, Rgb(40, 70, 40)); c_.Ring(148, 112, 22, Rgb(30, 60, 30)); c_.Fill(148, 16, 1, 192, Rgb(40, 80, 40)); c_.Fill(52, 112, 192, 1, Rgb(40, 80, 40));
            const bool ok = m::Abs(x) < 0.02F && m::Abs(y) < 0.02F;
            c_.Disc(148 - static_cast<int32_t>(x * 240.0F > 80.0F ? 80.0F : (x * 240.0F < -80.0F ? -80.0F : x * 240.0F)), 112 - static_cast<int32_t>(y * 240.0F > 80.0F ? 80.0F : (y * 240.0F < -80.0F ? -80.0F : y * 240.0F)), 16, ok ? kWhite : Rgb(230, 250, 170));
            char a[8], b[8]; Canvas::Format(a, Deg(x)); Canvas::Format(b, Deg(y)); c_.Text(70, 214, a, kWhite, kMedium, true); c_.Text(226, 214, b, kWhite, kMedium, true); c_.Text(kW / 2, 216, ok ? "LEVEL" : "", Rgb(150, 240, 130), kSmall, true);
        } else if (mode_ == 1) {
            const int32_t deg = Deg(x); c_.Number(kW / 2, 60, deg, kWhite, kTitle, true); c_.Text(kW / 2, 28, "EDGE ANGLE", kGrey, kSmall, true);
            const float ang = static_cast<float>(deg) / 57.3F; for (int32_t i = -120; i <= 120; i += 3) { c_.Fill(148 + static_cast<int32_t>(m::Cos(ang) * static_cast<float>(i)), 150 + static_cast<int32_t>(m::Sin(ang) * static_cast<float>(i)), 3, 5, Rgb(255, 200, 90)); }
            c_.Fill(28, 150, 240, 1, kDim);
        } else {
            c_.Text(kW / 2, 20, "MOTION", kGrey, kSmall, true);
            const int32_t h = m::Clamp(static_cast<int32_t>(t_.magnitude() * 40.0F), 0, 150), hp = m::Clamp(static_cast<int32_t>(peak_ * 40.0F), 0, 150);
            c_.Fill(70, 200 - h, 60, h, Rgb(255, 120, 90)); c_.Fill(170, 200 - hp, 60, hp, Rgb(255, 210, 110)); c_.Text(100, 206, "NOW", kGrey, kSmall, true); c_.Text(200, 206, "PEAK", kGrey, kSmall, true);
            c_.Text(16, 44, "Shakes", kGrey); c_.Number(16, 62, shakes_, kWhite, kLarge);
        }
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; float zx_{}, zy_{}, peak_{}; int32_t mode_{}, cool_{}, shakes_{};
};
}  // namespace
PORTKIT_MAIN(Game)
