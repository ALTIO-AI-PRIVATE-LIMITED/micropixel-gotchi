// SPDX-License-Identifier: Apache-2.0
//
// Small drawing and input layer for apps ported from Arduino displays. Each
// frame an app clears the canvas, draws rectangles, circles, lines and text
// in order, and presents. It sits on one colored-quad Scene batch and a pool of
// labels, so nothing here touches Host pixels directly.
#ifndef MICROPIXEL_APPS_PORTKIT_HPP
#define MICROPIXEL_APPS_PORTKIT_HPP

#include <stdint.h>

#include <new>
#include <optional>

#include "sdk/micropixel.hpp"

namespace portkit {

using micropixel::Color;
using micropixel::Duration;
using micropixel::Rect;
using micropixel::SystemFont;
using micropixel::Application;
using micropixel::KeyCode;

constexpr Color Rgb(uint8_t r, uint8_t g, uint8_t b) { return Color::Rgb(r, g, b); }

inline constexpr int32_t kW = 296, kH = 240;  // the Gotchi screen
inline constexpr SystemFont kSmall = SystemFont::kSmall, kMedium = SystemFont::kMedium, kLarge = SystemFont::kLarge,
                            kTitle = SystemFont::kTitle;
inline constexpr Color kWhite = Rgb(245, 245, 250), kGrey = Rgb(150, 155, 170), kDim = Rgb(70, 74, 90);

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

// A dim box in the middle of the screen with a title and a hint under it.
inline void Banner(Canvas& c, const char* title, const char* hint, Color accent) {
    c.Fill(0, 0, kW, kH, Rgb(0, 0, 0), 150U);
    c.Round(24, 74, kW - 48, 92, 12, Rgb(18, 20, 32));
    c.Frame(36, 84, kW - 72, 2, accent);
    c.Text(kW / 2, 96, title, accent, kLarge, true);
    c.Text(kW / 2, 134, hint, kWhite, kSmall, true);
}

inline void Beep(micropixel::Application& app, uint32_t hz, uint32_t ms, uint16_t volume = 200U,
                 micropixel::Waveform wave = micropixel::Waveform::kSine) {
    micropixel::Tone tone{};
    tone.waveform = wave;
    tone.frequency_hz = hz;
    tone.duration = Duration::Milliseconds(ms);
    tone.volume_per_mille = volume;
    (void)app.audio().Play(tone);
}

// Milliseconds since the app started, from the real clock rather than counted frames.
class Millis final {
   public:
    explicit Millis(micropixel::Application& app) : app_(app), start_(app.clock().Now()) {}
    [[nodiscard]] uint32_t Now() const { return static_cast<uint32_t>((app_.clock().Now() - start_).count_microseconds() / 1000U); }

   private:
    micropixel::Application& app_;
    micropixel::TimePoint start_;
};

// A saved number, such as a best score.
class Prefs final {
   public:
    explicit Prefs(micropixel::Application& app) : app_(app) {}
    [[nodiscard]] uint32_t Get(const char* key, uint32_t fallback = 0U) const { return app_.storage().GetU32Or(key, fallback); }
    void Set(const char* key, uint32_t value) { (void)app_.storage().SetU32(key, value); }
    // Raw bytes for small lists; the result is how many bytes were read.
    uint32_t Load(const char* key, uint8_t* bytes, uint32_t capacity) const {
        const auto got = app_.storage().GetBytes(key, bytes, capacity);
        return got ? *got : 0U;
    }
    void Save(const char* key, const uint8_t* bytes, uint32_t length) { (void)app_.storage().SetBytes(key, bytes, length); }

   private:
    micropixel::Application& app_;
};

// Tilt in screen terms: x is positive when the right edge is down, y when the
// bottom edge is down. Both are -1..1, and 0 when the Gotchi lies flat.
class Tilt final {
   public:
    explicit Tilt(micropixel::Application& app) {
        auto opened = app.sensors().OpenFirst<micropixel::Acceleration>(app.devices(), Duration::Milliseconds(20U));
        if (opened.has_value()) { sensor_.emplace(static_cast<micropixel::Accelerometer&&>(opened.value())); }
    }
    void Poll() {
        if (!sensor_) { return; }
        const auto sample = sensor_->Read();
        if (!sample) { return; }
        const auto& g = sample->value.meters_per_second_squared;
        x_ += (Unit(-g.x) - x_) * 0.35F;
        y_ += (Unit(g.y) - y_) * 0.35F;
        const float jolt = micropixel::math::Abs(g.x) + micropixel::math::Abs(g.y) + micropixel::math::Abs(g.z);
        jolt_ = jolt > 26.0F;
        magnitude_ = micropixel::math::Sqrt(g.x * g.x + g.y * g.y + g.z * g.z) / 9.8F;
    }
    [[nodiscard]] bool available() const { return sensor_.has_value(); }
    [[nodiscard]] float x() const { return x_; }
    [[nodiscard]] float y() const { return y_; }
    // True on a sharp shake or flick.
    [[nodiscard]] bool jolt() const { return jolt_; }
    // Total acceleration in g; about 1 when resting.
    [[nodiscard]] float magnitude() const { return magnitude_; }

   private:
    static float Unit(float value) { return micropixel::math::Clamp(value / 9.8F, -1.0F, 1.0F); }
    std::optional<micropixel::Accelerometer> sensor_{};
    float x_{}, y_{}, magnitude_{1.0F};
    bool jolt_{};
};

// Microphone samples at 16 kHz. Implement Mic() in the app to hear about new audio.
class Mic final {
   public:
    explicit Mic(micropixel::Application& app) : mic_(app.microphone()) {}
    bool Start() { return mic_.Start().has_value(); }
    void Stop() { (void)mic_.Stop(); }
    uint32_t Read(int16_t* samples, uint32_t max) {
        const uint32_t want = max > micropixel::Microphone::kMaxReadSamples ? micropixel::Microphone::kMaxReadSamples : max;
        const auto got = mic_.Read(samples, want);
        return got ? *got : 0U;
    }

   private:
    micropixel::Microphone mic_;
};

// Speaker stream at 16 kHz. Implement Feed() in the app to top it up.
class Speaker final {
   public:
    explicit Speaker(micropixel::Application& app) : app_(app) {}
    bool Open(uint32_t rate = 16000U) {
        micropixel::PcmStreamOptions options{};
        options.sample_rate = rate;
        options.channels = 1U;
        options.capacity_frames = 4096U;
        options.low_water_frames = 1024U;
        auto opened = app_.audio().OpenPcmStream(options);
        if (!opened) { return false; }
        stream_.emplace(static_cast<micropixel::PcmStream&&>(*opened));
        return true;
    }
    void Close() { stream_.reset(); }
    [[nodiscard]] bool open() const { return stream_.has_value(); }
    uint32_t Write(const int16_t* samples, uint32_t count) {
        if (!stream_) { return 0U; }
        const auto accepted = stream_->Write(samples, count);
        return accepted ? *accepted : 0U;
    }
    [[nodiscard]] micropixel::PcmStream* stream() { return stream_ ? &*stream_ : nullptr; }

   private:
    micropixel::Application& app_;
    std::optional<micropixel::PcmStream> stream_{};
};

// An on-screen keyboard for the lower half of the screen. Press() returns the
// key pressed: a letter, ' ', '\b' for delete, '\n' for done, or 0 for none.
class Keyboard final {
   public:
    static constexpr int32_t kTop = 128, kKeyH = 28;
    static char Press(int32_t x, int32_t y) {
        const int32_t row = (y - kTop) / kKeyH;
        if (y < kTop || row > 3) { return 0; }
        if (row == 3) { return x < 74 ? '\b' : (x < 222 ? ' ' : '\n'); }
        const char* keys = Row(row);
        const int32_t i = (x - Inset(row)) / 29;
        if (x < Inset(row) || i >= Len(keys)) { return 0; }
        return keys[i];
    }
    static void Draw(Canvas& c, Color key, Color ink) {
        for (int32_t row = 0; row < 3; ++row) {
            const char* keys = Row(row);
            for (int32_t i = 0; keys[i] != '\0'; ++i) {
                const int32_t x = Inset(row) + i * 29, y = kTop + row * kKeyH;
                c.Fill(x + 1, y + 1, 27, kKeyH - 2, key);
                char letter[2] = {keys[i], '\0'};
                c.Text(x + 15, y + 5, letter, ink, kSmall, true);
            }
        }
        const int32_t y = kTop + 3 * kKeyH;
        c.Fill(1, y + 1, 72, kKeyH - 2, key); c.Text(37, y + 5, "Del", ink, kSmall, true);
        c.Fill(75, y + 1, 146, kKeyH - 2, key); c.Text(148, y + 5, "Space", ink, kSmall, true);
        c.Fill(223, y + 1, 72, kKeyH - 2, key); c.Text(259, y + 5, "Done", ink, kSmall, true);
    }

   private:
    static const char* Row(int32_t row) { return row == 0 ? "QWERTYUIOP" : (row == 1 ? "ASDFGHJKL" : "ZXCVBNM"); }
    static int32_t Inset(int32_t row) { return row == 0 ? 3 : (row == 1 ? 17 : 46); }
    static int32_t Len(const char* s) { int32_t n = 0; while (s[n] != '\0') { ++n; } return n; }
};

inline constexpr auto kBoot = micropixel::KeyCode::kConfirm;
inline constexpr auto kVolUp = micropixel::KeyCode::kRight;
inline constexpr auto kVolDown = micropixel::KeyCode::kLeft;

// Runs a Game. Game must have Tick(uint32_t ms) and can add any of:
// Touch(const TouchEvent&), or Press(x, y), Drag(x, y), Tap(x, y); Key(const
// KeyEvent&), or Button(KeyCode); Mic(); Feed(); and speaker() returning a Speaker&.
template <typename Game>
int Run(micropixel::Application& app, Game& game, uint32_t tick_ms = 33U) {
    micropixel::Timer timer = app.timers().Every(Duration::Milliseconds(tick_ms)).value();
    app.Run([&](const micropixel::Event& event) {
        if (event.TimerFrom(timer) != nullptr) {
            game.Tick(tick_ms);
        } else if (const auto* touch = event.touch()) {
            if constexpr (requires { game.Touch(*touch); }) {
                game.Touch(*touch);
            } else {
                const int32_t x = touch->x(), y = touch->y();
                if (touch->phase() == micropixel::TouchPhase::kDown) {
                    if constexpr (requires { game.Press(x, y); }) { game.Press(x, y); }
                } else if (touch->phase() == micropixel::TouchPhase::kMove) {
                    if constexpr (requires { game.Drag(x, y); }) { game.Drag(x, y); }
                } else if (touch->phase() == micropixel::TouchPhase::kUp) {
                    if constexpr (requires { game.Tap(x, y); }) { game.Tap(x, y); }
                }
            }
        } else if (const auto* key = event.key()) {
            if constexpr (requires { game.Key(*key); }) {
                game.Key(*key);
            } else if constexpr (requires { game.Button(key->code()); }) {
                if (key->phase() == micropixel::KeyPhase::kDown) { game.Button(key->code()); }
            }
        } else if (event.microphone() != nullptr) {
            if constexpr (requires { game.Mic(); }) { game.Mic(); }
        } else {
            if constexpr (requires { game.Feed(); game.speaker(); }) {
                if (auto* stream = game.speaker().stream(); stream != nullptr && event.LowWaterFrom(*stream) != nullptr) {
                    game.Feed();
                }
            }
        }
        return micropixel::EventResult::kContinue;
    });
    return 0;
}

}  // namespace portkit

#define PORTKIT_MAIN(Game, ...)        \
    int main() {                       \
        micropixel::Application app;   \
        alignas(Game) static unsigned char storage[sizeof(Game)]; \
        Game& game = *new (storage) Game(app); \
        return portkit::Run(app, game __VA_OPT__(, ) __VA_ARGS__); \
    }

#endif
