// SPDX-License-Identifier: Apache-2.0
//
// Pocket Synth: a two-octave touch keyboard with four sounds and an octave shift.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kWhites = 14, kKeyW = 21, kTop = 90;
constexpr int8_t kSemi[kWhites] = {0,2,4,5,7,9,11,12,14,16,17,19,21,23};
constexpr int8_t kBlack[] = {1,3,6,8,10,13,15,18,20,22};
constexpr int8_t kBlackAt[] = {1,2,4,5,6,8,9,11,12,13};
const char* const kNames[] = {"SOFT", "BUZZ", "FLUTE", "NOISE"};
constexpr W kWaves[] = {W::kSine, W::kSquare, W::kTriangle, W::kNoise};
uint32_t Hz(int32_t semi) { float f = 130.81F; for (int32_t i = 0; i < semi; ++i) { f *= 1.0594631F; } return static_cast<uint32_t>(f); }
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(18, 18, 30)) {}
    void Tick(uint32_t) { ++f_; if (glow_ > 0) { --glow_; } Draw(); }
    void Press(int32_t x, int32_t y) {
        if (y < kTop - 34) { return; }
        if (y < kTop) { if (x < 196) { wave_ = x / 49; Beep(a_, 440U, 80U, 180U, kWaves[wave_]); } else if (x < 246) { oct_ = oct_ > -1 ? oct_ - 1 : -1; } else { oct_ = oct_ < 2 ? oct_ + 1 : 2; } return; }
        Play(x, y);
    }
    void Drag(int32_t x, int32_t y) { if (y >= kTop) { Play(x, y); } }
    void Tap(int32_t, int32_t) { key_ = -1; }
    void Button(KeyCode k) { if (k == kVolDown) { oct_ = oct_ > -1 ? oct_ - 1 : -1; } else if (k == kVolUp) { oct_ = oct_ < 2 ? oct_ + 1 : 2; } else if (k == kBoot) { wave_ = (wave_ + 1) % 4; } }

   private:
    int32_t KeyAt(int32_t x, int32_t y) const {
        if (y < kTop + 86) { for (size_t i = 0; i < sizeof(kBlack); ++i) { const int32_t bx = 1 + kBlackAt[i] * kKeyW - 7; if (x >= bx && x < bx + 14) { return kBlack[i]; } } }
        int32_t i = (x - 1) / kKeyW; i = i < 0 ? 0 : (i >= kWhites ? kWhites - 1 : i); return kSemi[i];
    }
    void Play(int32_t x, int32_t y) {
        const int32_t s = KeyAt(x, y); if (s == key_) { return; } key_ = s;
        micropixel::Tone t{}; t.waveform = kWaves[wave_]; t.frequency_hz = Hz(s + 12 * (oct_ + 1)) ; t.duration = Duration::Milliseconds(420U); t.volume_per_mille = 280U; t.attack = Duration::Milliseconds(6U); t.release = Duration::Milliseconds(160U);
        (void)a_.audio().Play(t); freq_ = t.frequency_hz; glow_ = 14;
    }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 4; ++i) { c_.Round(2 + i * 49, 58, 46, 28, 6, i == wave_ ? Rgb(255, 150, 80) : Rgb(50, 52, 76)); c_.Text(25 + i * 49, 64, kNames[i], kWhite, kSmall, true); }
        c_.Round(198, 58, 46, 28, 6, Rgb(50, 52, 76)); c_.Text(221, 64, "-", kWhite, kMedium, true);
        c_.Round(248, 58, 46, 28, 6, Rgb(50, 52, 76)); c_.Text(271, 64, "+", kWhite, kMedium, true);
        c_.Text(8, 8, "OCT", kGrey); c_.Number(38, 8, oct_ + 3, kWhite);
        const float amp = static_cast<float>(glow_) * 1.5F, w = static_cast<float>(freq_) * 0.00045F;
        for (int32_t x = 70; x < 290; x += 3) { const int32_t y = static_cast<int32_t>(m::Sin((static_cast<float>(x) + static_cast<float>(f_) * 8.0F) * (w + 0.03F)) * amp); c_.Fill(x, 31 + (y < 0 ? y : 0), 2, (y < 0 ? -y : y) + 3, Rgb(120, 240, 200)); }
        for (int32_t i = 0; i < kWhites; ++i) { c_.Fill(2 + i * kKeyW, kTop, kKeyW - 2, kH - kTop - 2, kSemi[i] == key_ ? Rgb(255, 190, 110) : Rgb(236, 236, 240)); }
        for (size_t i = 0; i < sizeof(kBlack); ++i) { c_.Fill(1 + kBlackAt[i] * kKeyW - 7, kTop, 14, 86, kBlack[i] == key_ ? Rgb(255, 120, 70) : Rgb(20, 20, 30)); }
        c_.Present();
    }
    Application& a_; Canvas c_;
    int32_t wave_{}, oct_{}, key_{-1}, f_{}, glow_{}; uint32_t freq_{440U};
};
}  // namespace
PORTKIT_MAIN(Game)
