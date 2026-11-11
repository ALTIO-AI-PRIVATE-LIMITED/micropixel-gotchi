// SPDX-License-Identifier: Apache-2.0
//
// Tempo Tuner: a metronome with tap tempo, and a mic tuner that names the note. Tap the top tab to switch.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kBuf = 1024;
int16_t pcm[kBuf];
const char* const kNote[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 22, 34)), mic_(a), ck_(a), p_(a) { bpm_ = static_cast<int32_t>(p_.Get("bpm", 100U)); }
    void Tick(uint32_t) { const uint32_t now = ck_.Now(); if (tab_ == 0 && run_ && now >= next_) { next_ = now + 60000U / static_cast<uint32_t>(bpm_); beat_ = (beat_ + 1) % 4; flash_ = 4; Beep(a_, beat_ == 0 ? 1320U : 880U, 34U, 260U, W::kSquare); } if (flash_ > 0) { --flash_; } Draw(); }
    void Mic() { if (tab_ != 1) { int16_t b[128]; (void)mic_.Read(b, 128U); return; } const uint32_t n = mic_.Read(pcm + fill_, static_cast<uint32_t>(kBuf - fill_)); fill_ += static_cast<int32_t>(n); if (fill_ >= kBuf) { fill_ = 0; Detect(); } }
    void Press(int32_t x, int32_t y) {
        if (y < 40) { Switch(x > kW / 2 ? 1 : 0); return; }
        if (tab_ == 1) { return; }
        if (y > 190) { if (x < 74) { Set(bpm_ - 5); } else if (x > 222) { Set(bpm_ + 5); } else { run_ = !run_; next_ = 0U; } return; }
        const uint32_t now = ck_.Now(); if (lastTap_ != 0U && now - lastTap_ < 2000U) { Set(static_cast<int32_t>(60000U / (now - lastTap_))); } lastTap_ = now;
    }
    void Button(KeyCode k) { if (k == kBoot) { Switch(1 - tab_); } else { Set(bpm_ + (k == kVolUp ? 1 : -1)); } }

   private:
    void Switch(int32_t t) { if (t == tab_) { return; } tab_ = t; if (t == 1) { mic_.Start(); } else { mic_.Stop(); } }
    void Set(int32_t v) { bpm_ = m::Clamp(v, 40, 240); p_.Set("bpm", static_cast<uint32_t>(bpm_)); }
    void Detect() {
        int64_t energy = 0; for (int32_t i = 0; i < kBuf; ++i) { energy += pcm[i] * pcm[i] / 256; }
        if (energy < 400000) { hz_ = 0.0F; return; }
        constexpr int32_t kLo = 24, kHi = 400; int64_t best = 0; int32_t lag = 0; bool rose = false; int64_t prev = 0;
        for (int32_t l = kLo; l < kHi; ++l) { int64_t s = 0; for (int32_t i = 0; i + l < kBuf; i += 2) { s += (pcm[i] * pcm[i + l]) / 256; } if (s > prev) { rose = true; } if (rose && s > best) { best = s; lag = l; } prev = s; }
        if (lag > 0) { hz_ = 16000.0F / static_cast<float>(lag); }
    }
    void Draw() {
        c_.Begin();
        for (int32_t t = 0; t < 2; ++t) { c_.Round(4 + t * 146, 4, 142, 32, 8, t == tab_ ? Rgb(80, 150, 255) : Rgb(40, 50, 76)); c_.Text(75 + t * 146, 11, t == 0 ? "Metronome" : "Tuner", kWhite, kSmall, true); }
        if (tab_ == 0) { c_.Number(kW / 2, 60, bpm_, kWhite, kTitle, true); c_.Text(kW / 2, 96, "tap here in time", kGrey, kSmall, true); for (int32_t i = 0; i < 4; ++i) { c_.Disc(88 + i * 40, 142, 14, i == beat_ && run_ ? (flash_ > 0 ? kWhite : Rgb(255, 170, 80)) : Rgb(52, 62, 90)); } c_.Round(6, 194, 64, 40, 8, Rgb(44, 54, 80)); c_.Text(38, 204, "-", kWhite, kMedium, true); c_.Round(226, 194, 64, 40, 8, Rgb(44, 54, 80)); c_.Text(258, 204, "+", kWhite, kMedium, true); c_.Round(78, 194, 140, 40, 8, run_ ? Rgb(200, 70, 80) : Rgb(70, 170, 110)); c_.Text(148, 206, run_ ? "Stop" : "Start", kWhite, kSmall, true); }
        else {
            if (hz_ <= 0.0F) { c_.Text(kW / 2, 110, "Play or sing a note", kGrey, kMedium, true); }
            else { float f = 16.3516F; int32_t note = 12; while (f * 1.0293F < hz_) { f *= 1.0594631F; ++note; } const float cents = (hz_ / f - 1.0F) / 0.0005778F; c_.Text(kW / 2, 60, kNote[note % 12], kWhite, kTitle, true); c_.Fill(28, 150, 240, 4, kDim); c_.Fill(147, 136, 2, 32, kWhite); const bool ok = m::Abs(cents) < 6.0F; c_.Disc(148 + static_cast<int32_t>(m::Clamp(cents, -50.0F, 50.0F) * 2.2F), 152, 11, ok ? Rgb(120, 240, 150) : Rgb(255, 140, 90)); c_.Number(kW / 2, 196, static_cast<int32_t>(hz_), kGrey, kSmall, true); }
        }
        c_.Present();
    }
    Application& a_; Canvas c_; portkit::Mic mic_; Millis ck_; Prefs p_; int32_t bpm_{100}, beat_{}, flash_{}, tab_{}, fill_{}; uint32_t next_{}, lastTap_{}; float hz_{}; bool run_{};
};
}  // namespace
PORTKIT_MAIN(Game)
