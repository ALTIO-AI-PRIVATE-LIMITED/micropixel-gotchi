// SPDX-License-Identifier: Apache-2.0
//
// Small drawing and input layer for apps ported from Arduino displays. Each
// frame an app clears the canvas, draws rectangles, circles, lines and text
// in order, and presents. It sits on one colored-quad Scene batch and a pool of
// labels, so nothing here touches Host pixels directly.
#ifndef MICROPIXEL_APPS_PORTKIT_HPP
#define MICROPIXEL_APPS_PORTKIT_HPP

#include <stdint.h>

#include <optional>

#include "sdk/micropixel.hpp"

namespace portkit {

using micropixel::Color;
using micropixel::Duration;
using micropixel::Rect;
using micropixel::SystemFont;

constexpr Color Rgb(uint8_t r, uint8_t g, uint8_t b) { return Color::Rgb(r, g, b); }

class Canvas final {
   public:
    Canvas(micropixel::Application& app, Color background, uint16_t quads = 1400U, uint16_t labels = 28U)
        : app_(app), scene_(app.renderer().CreateScene(background).value()) {
        const auto info = app.renderer().info();
        width_ = static_cast<int32_t>(info.width());
        height_ = static_cast<int32_t>(info.height());
        batch_ = scene_.CreateSpriteBatch(quads).value();
        capacity_ = quads;
        label_capacity_ = labels > kMaxLabels ? kMaxLabels : labels;
        for (uint16_t i = 0U; i < label_capacity_; ++i) {
            labels_[i] = scene_.CreateLabel({0, 0}, "", Color::White(), SystemFont::kSmall).value();
            labels_[i].SetVisible(false);
        }
    }

    [[nodiscard]] int32_t width() const { return width_; }
    [[nodiscard]] int32_t height() const { return height_; }
    void SetBackground(Color color) { scene_.SetBackground(color); }

    void Begin() {
        quads_used_ = 0U;
        labels_used_ = 0U;
    }

    void Fill(int32_t x, int32_t y, int32_t w, int32_t h, Color color, uint8_t opacity = 255U) {
        if (w <= 0 || h <= 0 || quads_used_ >= capacity_) {
            return;
        }
        micropixel::SpriteInstance quad{};
        quad.destination = {x, y, w, h};
        quad.source = {0, 0, 1, 1};
        quad.color = color;
        quad.opacity = opacity;
        quad.visible = true;
        batch_.SetInstance(quads_used_++, quad);
    }

    void Frame(int32_t x, int32_t y, int32_t w, int32_t h, Color color, int32_t t = 1) {
        Fill(x, y, w, t, color);
        Fill(x, y + h - t, w, t, color);
        Fill(x, y + t, t, h - 2 * t, color);
        Fill(x + w - t, y + t, t, h - 2 * t, color);
    }

    // Rounded box built from a few stacked spans.
    void Round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, Color color) {
        for (int32_t i = 0; i < r; ++i) {
            const int32_t inset = r - IntSqrt(r * r - (r - i) * (r - i));
            Fill(x + inset, y + i, w - 2 * inset, 1, color);
            Fill(x + inset, y + h - 1 - i, w - 2 * inset, 1, color);
        }
        Fill(x, y + r, w, h - 2 * r, color);
    }

    void Disc(int32_t cx, int32_t cy, int32_t r, Color color, uint8_t opacity = 255U) {
        for (int32_t dy = -r; dy <= r; ++dy) {
            const int32_t dx = IntSqrt(r * r - dy * dy);
            Fill(cx - dx, cy + dy, 2 * dx + 1, 1, color, opacity);
        }
    }

    void Ring(int32_t cx, int32_t cy, int32_t r, Color color) {
        int32_t x = r, y = 0, err = 1 - r;
        while (x >= y) {
            Fill(cx - x, cy + y, 1, 1, color); Fill(cx + x, cy + y, 1, 1, color);
            Fill(cx - x, cy - y, 1, 1, color); Fill(cx + x, cy - y, 1, 1, color);
            Fill(cx - y, cy + x, 1, 1, color); Fill(cx + y, cy + x, 1, 1, color);
            Fill(cx - y, cy - x, 1, 1, color); Fill(cx + y, cy - x, 1, 1, color);
            ++y;
            if (err < 0) { err += 2 * y + 1; } else { --x; err += 2 * (y - x) + 1; }
        }
    }

    void Line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, Color color, int32_t t = 1) {
        int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
        const int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int32_t err = dx + dy;
        for (;;) {
            Fill(x0, y0, t, t, color);
            if (x0 == x1 && y0 == y1) { break; }
            const int32_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void Text(int32_t x, int32_t y, const char* text, Color color, SystemFont font = SystemFont::kSmall,
              bool centered = false) {
        if (labels_used_ >= label_capacity_) {
            return;
        }
        auto& label = labels_[labels_used_++];
        label.SetPosition({x, y});
        label.SetFont(font);
        label.SetColor(color);
        label.SetCentered(centered);
        label.SetText(text);
        label.SetVisible(true);
    }

    void Number(int32_t x, int32_t y, int32_t value, Color color, SystemFont font = SystemFont::kSmall,
                bool centered = false) {
        char text[12];
        Format(text, value);
        Text(x, y, text, color, font, centered);
    }

    void Present() {
        for (uint16_t i = quads_used_; i < last_quads_; ++i) {
            batch_.SetInstanceVisible(i, false);
        }
        for (uint16_t i = labels_used_; i < last_labels_; ++i) {
            labels_[i].SetVisible(false);
        }
        last_quads_ = quads_used_;
        last_labels_ = labels_used_;
        (void)app_.renderer().Present(scene_);
    }

    static void Format(char* out, int32_t value) {
        char digits[11];
        uint32_t n = value < 0 ? static_cast<uint32_t>(-value) : static_cast<uint32_t>(value), count = 0U;
        do { digits[count++] = static_cast<char>('0' + n % 10U); n /= 10U; } while (n != 0U);
        if (value < 0) { *out++ = '-'; }
        while (count != 0U) { *out++ = digits[--count]; }
        *out = '\0';
    }

    static int32_t IntSqrt(int32_t value) {
        if (value <= 0) { return 0; }
        int32_t root = 0;
        while ((root + 1) * (root + 1) <= value) { ++root; }
        return root;
    }

   private:
    static constexpr uint16_t kMaxLabels = 48U;
    micropixel::Application& app_;
    micropixel::Scene scene_;
    micropixel::SpriteBatch batch_{};
    micropixel::LabelNode labels_[kMaxLabels]{};
    uint16_t capacity_{}, quads_used_{}, last_quads_{};
    uint16_t label_capacity_{}, labels_used_{}, last_labels_{};
    int32_t width_{}, height_{};
};

struct Box final {
    int32_t x{}, y{}, w{}, h{};
    [[nodiscard]] bool Has(int32_t px, int32_t py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// Draws a tappable button.
inline void Button(Canvas& canvas, const Box& box, const char* text, Color fill, Color ink,
                   SystemFont font = SystemFont::kSmall) {
    canvas.Round(box.x, box.y, box.w, box.h, 7, fill);
    canvas.Text(box.x + box.w / 2, box.y + (box.h - 16) / 2, text, ink, font, true);
}

inline void Beep(micropixel::Application& app, uint32_t hz, uint32_t ms, uint16_t volume = 200U) {
    micropixel::Tone tone{};
    tone.frequency_hz = hz;
    tone.duration = Duration::Milliseconds(ms);
    tone.volume_per_mille = volume;
    (void)app.audio().Play(tone);
}

// Runs `game`, which provides Tick(uint32_t ms), Touch(const TouchEvent&) and
// Key(const KeyEvent&).
template <typename Game>
int Run(micropixel::Application& app, Game& game, uint32_t tick_ms = 33U) {
    micropixel::Timer timer = app.timers().Every(Duration::Milliseconds(tick_ms)).value();
    app.Run([&](const micropixel::Event& event) {
        if (const auto* fired = event.TimerFrom(timer)) {
            (void)fired;
            game.Tick(tick_ms);
        } else if (const auto* touch = event.touch()) {
            game.Touch(*touch);
        } else if (const auto* key = event.key()) {
            game.Key(*key);
        }
        return micropixel::EventResult::kContinue;
    });
    return 0;
}

}  // namespace portkit

#endif
