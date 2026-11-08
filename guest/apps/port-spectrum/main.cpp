// SPDX-License-Identifier: MIT
//
// Live audio spectrum for the Gotchi, after M5SpectrumAnalyzer by mbogatyr
// (https://github.com/mbogatyr/M5SpectrumAnalyzer), MIT. A 512-point FFT of the
// microphone at 16 kHz, grouped into 32 logarithmic bands with falling peaks.
//
// Controls: tap or BOOT to change the color theme.
#include "apps/portkit/portkit.hpp"

namespace {

namespace math = micropixel::math;
using micropixel::Application;
using portkit::Canvas;
using portkit::Rgb;

constexpr uint32_t kFft = 512U;
constexpr int32_t kBands = 32;
int16_t samples[kFft];
float real[kFft], imag[kFft], window_fn[kFft];
uint32_t filled;

void Fft() {
    for (uint32_t i = 1U, j = 0U; i < kFft; ++i) {
        uint32_t bit = kFft >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) { j ^= bit; }
        j ^= bit;
        if (i < j) {
            const float a = real[i]; real[i] = real[j]; real[j] = a;
            const float b = imag[i]; imag[i] = imag[j]; imag[j] = b;
        }
    }
    for (uint32_t length = 2U; length <= kFft; length <<= 1U) {
        const float angle = -math::kTwoPi / static_cast<float>(length);
        const float wr = math::Cos(angle), wi = math::Sin(angle);
        for (uint32_t i = 0U; i < kFft; i += length) {
            float cr = 1.0F, ci = 0.0F;
            for (uint32_t j = 0U; j < length / 2U; ++j) {
                const uint32_t a = i + j, b = a + length / 2U;
                const float tr = real[b] * cr - imag[b] * ci, ti = real[b] * ci + imag[b] * cr;
                real[b] = real[a] - tr; imag[b] = imag[a] - ti;
                real[a] += tr; imag[a] += ti;
                const float next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = next;
            }
        }
    }
}

class Spectrum final {
   public:
    explicit Spectrum(Application& app) : canvas_(app, Rgb(6, 8, 18), 900U, 6U), mic_(app.microphone()) {
        for (uint32_t i = 0U; i < kFft; ++i) {
            window_fn[i] = 0.5F - 0.5F * math::Cos(math::kTwoPi * static_cast<float>(i) / (kFft - 1U));
        }
        // Band edges follow a log curve from 100 Hz to 8 kHz in FFT bins.
        for (int32_t band = 0; band <= kBands; ++band) {
            float bin = 3.2F;
            for (int32_t step = 0; step < band; ++step) { bin *= 1.1465F; }
            edges_[band] = static_cast<uint16_t>(bin);
            if (band > 0 && edges_[band] <= edges_[band - 1]) { edges_[band] = edges_[band - 1] + 1U; }
        }
        ok_ = mic_.Start().has_value();
        Draw();
    }

    void Capture() {
        for (;;) {
            const uint32_t room = kFft - filled;
            const auto got = mic_.Read(samples + filled, math::Min(room, micropixel::Microphone::kMaxReadSamples));
            if (!got || *got == 0U) { return; }
            filled += *got;
            if (filled == kFft) { Analyze(); filled = 0U; }
        }
    }

    void Tick() {
        for (int32_t b = 0; b < kBands; ++b) {
            if (peaks_[b] > 0.0F) { peaks_[b] = math::Max(peaks_[b] - 2.5F, 0.0F); }
        }
        Draw();
    }

    void Touch() {
        theme_ = (theme_ + 1) % 3;
    }

   private:
    void Analyze() {
        for (uint32_t i = 0U; i < kFft; ++i) {
            real[i] = (static_cast<float>(samples[i]) / 32768.0F) * window_fn[i];
            imag[i] = 0.0F;
        }
        Fft();
        for (int32_t b = 0; b < kBands; ++b) {
            float power = 0.0F;
            for (uint32_t i = edges_[b]; i < edges_[b + 1] && i < kFft / 2U; ++i) {
                power = math::Max(power, real[i] * real[i] + imag[i] * imag[i]);
            }
            // Approximate dB through the float exponent, then map -70..0 dB to 0..1.
            const float target = math::Clamp((Decibels(power) + 62.0F) / 62.0F, 0.0F, 1.0F) * kBarHeight;
            levels_[b] += (target - levels_[b]) * (target > levels_[b] ? 0.65F : 0.18F);
            if (levels_[b] > peaks_[b]) { peaks_[b] = levels_[b]; }
        }
    }

    static float Decibels(float power) {
        if (power <= 1e-9F) { return -90.0F; }
        int32_t exponent = 0;
        while (power < 1.0F) { power *= 2.0F; --exponent; }
        while (power >= 2.0F) { power /= 2.0F; ++exponent; }
        return 3.0103F * (static_cast<float>(exponent) + (power - 1.0F));
    }

    void Draw() {
        static constexpr portkit::Color kLow[3] = {Rgb(40, 210, 150), Rgb(90, 130, 255), Rgb(255, 140, 50)};
        static constexpr portkit::Color kHigh[3] = {Rgb(250, 90, 80), Rgb(240, 110, 255), Rgb(255, 245, 140)};
        canvas_.Begin();
        canvas_.Text(10, 6, ok_ ? "SPECTRUM" : "MIC NOT AVAILABLE", Rgb(170, 180, 200));
        canvas_.Text(canvas_.width() - 10 - 72, 6, "100-8k Hz", Rgb(90, 100, 120));
        const int32_t base = canvas_.height() - 8, bar = (canvas_.width() - 16) / kBands;
        for (int32_t b = 0; b < kBands; ++b) {
            const int32_t x = 8 + b * bar;
            const int32_t height = static_cast<int32_t>(levels_[b]);
            for (int32_t s = 0; s < height / 6 + 1 && height > 0; ++s) {
                const int32_t t = math::Min(s * 6, kBarHeight);
                canvas_.Fill(x, base - (s + 1) * 6 + 1, bar - 2, 5,
                             portkit::Color::Mix(kHigh[theme_], kLow[theme_], static_cast<uint8_t>(t * 255 / kBarHeight)));
            }
            canvas_.Fill(x, base - static_cast<int32_t>(peaks_[b]) - 2, bar - 2, 2, Rgb(240, 240, 250));
        }
        canvas_.Present();
    }

    static constexpr int32_t kBarHeight = 196;
    Canvas canvas_;
    micropixel::Microphone mic_;
    uint16_t edges_[kBands + 1]{};
    float levels_[kBands]{}, peaks_[kBands]{};
    int32_t theme_{};
    bool ok_{};
};

}  // namespace

int main() {
    Application app;
    Spectrum game(app);
    auto timer = app.timers().Every(micropixel::Duration::Milliseconds(40)).value();
    app.Run([&](const micropixel::Event& event) {
        if (event.microphone() != nullptr) {
            game.Capture();
        } else if (event.TimerFrom(timer) != nullptr) {
            game.Tick();
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kUp) { game.Touch(); }
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown) { game.Touch(); }
        }
        return micropixel::EventResult::kContinue;
    });
    return 0;
}
