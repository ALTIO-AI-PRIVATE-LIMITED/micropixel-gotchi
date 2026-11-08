// SPDX-License-Identifier: MIT
//
// Mandelbrot explorer for the Gotchi, after M5Fractal by mbogatyr
// (https://github.com/mbogatyr/M5Fractal), MIT. Renders in two progressive
// passes in 4-pixel blocks, since the app draws colored rectangles.
//
// Controls: tap to zoom into a point, VOL+ to zoom out, VOL- for the next
// palette, BOOT to reset the view.
#include "apps/portkit/portkit.hpp"

namespace {

using micropixel::Application;
using portkit::Canvas;
using portkit::Color;
using portkit::Rgb;

constexpr int32_t kCell = 4, kCols = 74, kRows = 60, kMaxIterations = 96;
constexpr int32_t kBudget = 3500;  // iterations per tick, about a frame of soft float
constexpr int32_t kShades = 32;

uint8_t shade[kCols][kRows];

Color Blend(Color a, Color b, int32_t t, int32_t range) {
    return Color::Mix(b, a, static_cast<uint8_t>(t * 255 / range));
}

class Mandelbrot final {
   public:
    explicit Mandelbrot(Application& app) : app_(app), canvas_(app, Rgb(0, 0, 0), 4600U, 4U) { Restart(); }

    void Tick(uint32_t) {
        if (pass_ > 1) { return; }
        int32_t budget = kBudget;
        const int32_t step = pass_ == 0 ? 2 : 1;
        while (budget > 0 && pass_ <= 1) {
            if (pass_ == 0 || (x_ % 2 != 0 || y_ % 2 != 0)) {
                const int32_t value = Sample(x_, y_, budget);
                for (int32_t i = 0; i < step; ++i) {
                    for (int32_t j = 0; j < step; ++j) {
                        if (x_ + i < kCols && y_ + j < kRows) { shade[x_ + i][y_ + j] = static_cast<uint8_t>(value); }
                    }
                }
            }
            x_ += step;
            if (x_ >= kCols) { x_ = 0; y_ += step; }
            if (y_ >= kRows) { x_ = 0; y_ = 0; ++pass_; if (pass_ == 2) { break; } }
        }
        Draw();
    }

    void Touch(const micropixel::TouchEvent& touch) {
        if (touch.phase() != micropixel::TouchPhase::kUp) { return; }
        cx_ += (static_cast<double>(touch.x()) - canvas_.width() / 2.0) * scale_;
        cy_ += (static_cast<double>(touch.y()) - canvas_.height() / 2.0) * scale_;
        scale_ /= 2.0;
        ++zoom_;
        portkit::Beep(app_, 500U + 40U * static_cast<uint32_t>(zoom_), 40U, 120U);
        Restart();
    }

    void Key(const micropixel::KeyEvent& key) {
        if (key.phase() != micropixel::KeyPhase::kDown) { return; }
        if (key.code() == micropixel::KeyCode::kConfirm) {
            cx_ = -0.6; cy_ = 0.0; scale_ = 3.2 / 296.0; zoom_ = 0;
            Restart();
        } else if (key.code() == micropixel::KeyCode::kRight) {
            if (zoom_ > 0) { scale_ *= 2.0; --zoom_; Restart(); }
        } else if (key.code() == micropixel::KeyCode::kLeft) {
            palette_ = (palette_ + 1) % 3;
                Draw();
        }
    }

   private:
    void Restart() {
        pass_ = 0; x_ = 0; y_ = 0;
    }

    [[nodiscard]] Color Shade(int32_t i) const {
        static constexpr Color kStops[3][4] = {
            {Rgb(8, 10, 60), Rgb(30, 120, 220), Rgb(250, 230, 150), Rgb(190, 60, 20)},
            {Rgb(30, 0, 40), Rgb(190, 30, 90), Rgb(255, 160, 40), Rgb(255, 250, 200)},
            {Rgb(0, 20, 20), Rgb(0, 140, 110), Rgb(170, 240, 120), Rgb(20, 80, 140)},
        };
        const int32_t segment = i * 3 / kShades, within = i * 3 - segment * kShades;
        return Blend(kStops[palette_][segment], kStops[palette_][segment + 1], within, kShades);
    }

    int32_t Sample(int32_t col, int32_t row, int32_t& budget) {
        const double re = cx_ + ((col * kCell + kCell / 2) - canvas_.width() / 2.0) * scale_;
        const double im = cy_ + ((row * kCell + kCell / 2) - canvas_.height() / 2.0) * scale_;
        double zr = 0.0, zi = 0.0;
        const int32_t limit = kMaxIterations + zoom_ * 12;
        for (int32_t i = 0; i < limit; ++i) {
            const double r2 = zr * zr, i2 = zi * zi;
            if (r2 + i2 > 4.0) { budget -= i + 1; return 1 + i % (kShades - 1); }
            zi = 2.0 * zr * zi + im;
            zr = r2 - i2 + re;
        }
        budget -= limit;
        return 0;
    }

    void Draw() {
        canvas_.Begin();
        for (int32_t y = 0; y < kRows; ++y) {
            int32_t start = 0;
            for (int32_t x = 1; x <= kCols; ++x) {
                if (x == kCols || shade[x][y] != shade[start][y]) {
                    if (shade[start][y] != 0U) {
                        canvas_.Fill(start * kCell, y * kCell, (x - start) * kCell, kCell, Shade(shade[start][y]));
                    }
                    start = x;
                }
            }
        }
        canvas_.Fill(4, 4, 74, 20, Rgb(0, 0, 0), 170U);
        canvas_.Text(8, 6, "ZOOM", Rgb(180, 180, 190));
        canvas_.Number(52, 6, zoom_, Rgb(255, 255, 255));
        canvas_.Present();
    }

    Application& app_;
    Canvas canvas_;
    double cx_{-0.6}, cy_{0.0}, scale_{3.2 / 296.0};
    int32_t zoom_{}, palette_{}, pass_{}, x_{}, y_{};
};

}  // namespace

int main() {
    Application app;
    Mandelbrot game(app);
    return portkit::Run(app, game, 40U);
}
