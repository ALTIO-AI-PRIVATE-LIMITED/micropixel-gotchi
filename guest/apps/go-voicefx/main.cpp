// SPDX-License-Identifier: Apache-2.0
//
// Silly Voice: record three seconds and hear it as a chipmunk, a giant, a robot or backwards. Four sound effects too.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kMax = 24000;  // 3 s at 8 kHz
int8_t clip[kMax];
const char* const kFx[] = {"Chipmunk", "Giant", "Robot", "Backward"};
const char* const kSfx[] = {"Boing", "Laser", "Horn", "Tada"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(40, 18, 70)), mic_(a), spk_(a) {}
    Speaker& speaker() { return spk_; }
    void Tick(uint32_t) { Pump(); Sfx(); ++f_; Draw(); }
    void Mic() { if (!rec_) { int16_t b[256]; (void)mic_.Read(b, 256U); return; } int16_t b[320]; const uint32_t n = mic_.Read(b, 320U); for (uint32_t i = 0U; i + 1U < n && len_ < kMax; i += 2U) { clip[len_++] = static_cast<int8_t>(b[i] / 256); level_ = m::Max(level_, m::Abs(static_cast<int32_t>(b[i]))); } if (len_ >= kMax) { Stop(); } }
    void Feed() { Pump(); }
    void Press(int32_t x, int32_t y) {
        if (y < 94) { spk_.Close(); pl_ = false; if (mic_.Start()) { rec_ = true; len_ = 0; } return; }
        if (y < 164) { const int32_t i = m::Clamp(x / 74, 0, 3); Play(i); return; }
        const int32_t i = m::Clamp(x / 74, 0, 3); Fire(i);
    }
    void Tap(int32_t, int32_t) { Stop(); }
    void Button(KeyCode k) { if (k == kBoot) { if (rec_) { Stop(); } else { spk_.Close(); pl_ = false; if (mic_.Start()) { rec_ = true; len_ = 0; } } } }

   private:
    void Stop() { if (rec_) { rec_ = false; mic_.Stop(); } }
    void Play(int32_t fx) { if (len_ < 800 || rec_) { return; } fx_ = fx; pos_ = fx == 3 ? static_cast<float>(len_ - 1) : 0.0F; step_ = fx == 0 ? 0.85F : (fx == 1 ? 0.32F : (fx == 3 ? -0.5F : 0.5F)); phase_ = 0.0F; if (spk_.open() || spk_.Open()) { pl_ = true; } }
    void Pump() {
        if (!pl_ || !spk_.open()) { return; }
        for (int32_t guard = 0; guard < 6; ++guard) {
            int16_t out[256]; uint32_t n = 0U; const float start = pos_, ph0 = phase_;
            for (; n < 256U && pos_ >= 0.0F && pos_ < static_cast<float>(len_ - 1); ++n) { const auto i = static_cast<int32_t>(pos_); const float fr = pos_ - static_cast<float>(i); float v = static_cast<float>(clip[i]) * (1.0F - fr) + static_cast<float>(clip[i + 1]) * fr; if (fx_ == 2) { phase_ += 0.11F; v *= m::Sin(phase_) * 1.5F; } out[n] = static_cast<int16_t>(m::Clamp(v * 240.0F, -30000.0F, 30000.0F)); pos_ += step_; }
            if (n == 0U) { pl_ = false; return; }
            const uint32_t took = spk_.Write(out, n);
            if (took < n) { pos_ = start + static_cast<float>(took) * step_; phase_ = ph0 + static_cast<float>(took) * 0.11F; return; }
        }
    }
    void Fire(int32_t i) { sfx_ = i; sfxT_ = 0; }
    void Sfx() {
        if (sfx_ < 0) { return; }
        const int32_t t = sfxT_++;
        if (sfx_ == 0) { if (t < 9) { Beep(a_, 180U + static_cast<uint32_t>((t % 3) * 110 + t * 30), 40U, 240U, W::kTriangle); } else { sfx_ = -1; } }
        else if (sfx_ == 1) { if (t < 9) { Beep(a_, 1800U - static_cast<uint32_t>(t) * 180U, 34U, 200U, W::kSquare); } else { sfx_ = -1; } }
        else if (sfx_ == 2) { if (t == 0) { Beep(a_, 220U, 500U, 340U, W::kSquare); } else if (t > 16) { sfx_ = -1; } }
        else { if (t == 0) { Beep(a_, 523U, 120U); } else if (t == 5) { Beep(a_, 659U, 120U); } else if (t == 10) { Beep(a_, 784U, 300U); } else if (t > 20) { sfx_ = -1; } }
    }
    void Draw() {
        c_.Begin();
        c_.Round(10, 8, kW - 20, 80, 14, rec_ ? Rgb(220, 60, 80) : Rgb(70, 40, 120));
        c_.Text(kW / 2, 16, rec_ ? "Listening" : "Hold here and talk", kWhite, kMedium, true);
        const int32_t w = len_ * 240 / kMax; c_.Fill(28, 60, 240, 10, Rgb(40, 20, 60)); c_.Fill(28, 60, w, 10, Rgb(255, 210, 110));
        if (level_ > 0) { level_ = level_ * 9 / 10; }
        for (int32_t i = 0; i < 4; ++i) { c_.Round(3 + i * 74, 96, 68, 62, 10, pl_ && fx_ == i ? Rgb(255, 170, 70) : Rgb(120, 60, 190)); c_.Text(37 + i * 74, 118, kFx[i], kWhite, kSmall, true); }
        for (int32_t i = 0; i < 4; ++i) { c_.Round(3 + i * 74, 170, 68, 62, 10, Rgb(30, 110, 130)); c_.Text(37 + i * 74, 192, kSfx[i], kWhite, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; portkit::Mic mic_; Speaker spk_; int32_t len_{}, fx_{}, f_{}, sfx_{-1}, sfxT_{}, level_{}; float pos_{}, step_{1.0F}, phase_{}; bool rec_{}, pl_{};
};
}  // namespace
PORTKIT_MAIN(Game)
