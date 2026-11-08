// SPDX-License-Identifier: Apache-2.0
//
// Liquid Hourglass for the Gotchi. Port of VibeStick Hourglass Liquid by
// oliverxing2025 (https://github.com/oliverxing2025/VibeStick-Hourglass-Liquid).
// hourglass_physics.c/.h are the original simulation, unchanged. This file
// replaces the board code: accelerometer, drawing and controls.
//
// Controls: tap to restart, BOOT to change the timer length. Turn the Gotchi
// upside down to flip the glass.
extern "C" {
#include "apps/port-hourglass/hourglass_physics.h"
}
#include "apps/portkit/portkit.hpp"

namespace {

namespace math = micropixel::math;
using micropixel::Application;
using portkit::Canvas;
using portkit::Rgb;

constexpr int32_t kDurations[] = {60, 30, 180, 300};
constexpr int32_t kScale = 2, kOffsetY = 2;

class Hourglass final {
   public:
    explicit Hourglass(Application& app) : canvas_(app, Rgb(12, 14, 26), 1400U, 6U) {
        auto opened = app.sensors().OpenFirst<micropixel::Acceleration>(app.devices(), micropixel::Duration::Milliseconds(20U));
        if (opened.has_value()) { accel_.emplace(static_cast<micropixel::Accelerometer&&>(opened.value())); }
        hg_physics_init();
        hg_physics_set_limits(HG_DEFAULT_MAX_ACTIVE, HG_DEFAULT_CONSTRAINT_ITERATIONS);
        hg_physics_reset();
    }

    void Tick(uint32_t ms) {
        Sense();
        elapsed_ms_ += ms;
        const uint32_t total = static_cast<uint32_t>(kDurations[duration_]) * 1000U;
        if (elapsed_ms_ > total) { elapsed_ms_ = total; }
        hg_physics_step(gravity_x_, gravity_y_, static_cast<float>(elapsed_ms_) / static_cast<float>(total), true);
        Draw();
    }

    void Touch(const micropixel::TouchEvent& touch) {
        if (touch.phase() == micropixel::TouchPhase::kUp) { Restart(); }
    }

    void Key(const micropixel::KeyEvent& key) {
        if (key.phase() == micropixel::KeyPhase::kDown && key.code() == micropixel::KeyCode::kConfirm) {
            duration_ = (duration_ + 1U) % 4U;
            Restart();
        }
    }

   private:
    void Restart() {
        elapsed_ms_ = 0U;
        hg_physics_reset();
    }

    void Sense() {
        if (!accel_) { return; }
        auto sample = accel_->Read();
        if (!sample) { return; }
        const auto& g = sample->value.meters_per_second_squared;
        const float x = -g.x, y = g.y;
        const float length = math::Sqrt(x * x + y * y);
        // Flat on a table there is no sideways pull, so treat it as upright.
        const float nx = length > 3.0F ? x / length : 0.0F;
        const float ny = length > 3.0F ? y / length : 1.0F;
        const bool wants_inverted = ny < -0.5F;
        if (wants_inverted != inverted_) {
            inverted_ = wants_inverted;
            const uint32_t total = static_cast<uint32_t>(kDurations[duration_]) * 1000U;
            elapsed_ms_ = total - elapsed_ms_;
            hg_physics_reset();
        }
        gravity_x_ += ((inverted_ ? -nx : nx) - gravity_x_) * 0.3F;
        gravity_y_ += (math::Abs(ny) - gravity_y_) * 0.3F;
        const float norm = math::Sqrt(gravity_x_ * gravity_x_ + gravity_y_ * gravity_y_);
        if (norm > 0.01F) { gravity_x_ /= norm; gravity_y_ /= norm; }
    }

    [[nodiscard]] int32_t X(float x) const { return canvas_.width() / 2 + static_cast<int32_t>((x - 57.0F) * kScale); }
    [[nodiscard]] int32_t Y(float y) const {
        const int32_t py = static_cast<int32_t>(y * kScale) + kOffsetY;
        return inverted_ ? canvas_.height() - py : py;
    }

    void Draw() {
        canvas_.Begin();
        for (int32_t y = 10; y <= 108; y += 2) {
            const int32_t left = X(hg_geometry_left_boundary(y)), right = X(hg_geometry_right_boundary(y));
            canvas_.Fill(left - 3, Y(static_cast<float>(y)), 3, 4, Rgb(120, 170, 220));
            canvas_.Fill(right + 1, Y(static_cast<float>(y)), 3, 4, Rgb(120, 170, 220));
        }
        const hg_particle_t* particles = hg_physics_particles();
        const uint16_t count = hg_physics_particle_count();
        for (uint16_t i = 0U; i < count; ++i) {
            const hg_particle_t& p = particles[i];
            if (p.state == HG_PARTICLE_UNUSED) { continue; }
            const auto color = p.color_phase % 2U == 0U ? Rgb(255, 196, 80) : Rgb(244, 160, 60);
            canvas_.Fill(X(p.x) - 2, Y(p.y) - 2, 4, 4, color);
        }
        const uint32_t total = static_cast<uint32_t>(kDurations[duration_]) * 1000U;
        char text[12];
        Canvas::Format(text, static_cast<int32_t>((total - elapsed_ms_ + 999U) / 1000U));
        canvas_.Text(10, 8, "SEC", Rgb(130, 140, 170));
        canvas_.Text(10, 26, text, Rgb(250, 250, 250), micropixel::SystemFont::kLarge);
        canvas_.Present();
    }

    Canvas canvas_;
    std::optional<micropixel::Accelerometer> accel_{};
    uint32_t elapsed_ms_{}, duration_{};
    float gravity_x_{0.0F}, gravity_y_{1.0F};
    bool inverted_{};
};

}  // namespace

int main() {
    Application app;
    Hourglass game(app);
    return portkit::Run(app, game, 1000U / HG_PHYSICS_HZ);
}
