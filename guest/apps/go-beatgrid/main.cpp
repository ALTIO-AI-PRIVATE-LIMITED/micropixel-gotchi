// SPDX-License-Identifier: Apache-2.0
//
// Beat Grid: a four-track sixteen-step drum machine. Tap cells to place hits.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kSteps = 16, kTracks = 4, kCW = 18, kCH = 40, kX = 4, kY = 50;
constexpr Color kK[] = {Rgb(255,110,90), Rgb(255,200,90), Rgb(120,220,255), Rgb(190,140,255)};
const char* const kBtn[] = {"PLAY", "-", "+", "CLEAR"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 16, 28)), p_(a), ck_(a) { Demo(); bpm_ = p_.Get("bpm", 112U); }
    void Tick(uint32_t) {
        const uint32_t now = ck_.Now(), gap = 15000U / bpm_;
        if (playing_ && now >= next_) { next_ = now + gap; step_ = (step_ + 1) % kSteps; Hit(); }
        Draw();
    }
    void Press(int32_t x, int32_t y) {
        if (y < 44) { const int32_t b = x / 74; if (b == 0) { playing_ = !playing_; step_ = -1; next_ = 0U; } else if (b == 1) { Bpm(-6); } else if (b == 2) { Bpm(6); } else { for (auto& r : g_) { for (auto& v : r) { v = false; } } } return; }
        const int32_t s = (x - kX) / kCW, t = (y - kY) / kCH;
        if (s >= 0 && s < kSteps && t >= 0 && t < kTracks) { g_[t][s] = !g_[t][s]; }
    }
    void Button(KeyCode k) { if (k == kBoot) { playing_ = !playing_; step_ = -1; next_ = 0U; } else { Bpm(k == kVolUp ? 6 : -6); } }

   private:
    void Bpm(int32_t d) { int32_t n = static_cast<int32_t>(bpm_) + d; n = n < 60 ? 60 : (n > 200 ? 200 : n); bpm_ = static_cast<uint32_t>(n); p_.Set("bpm", bpm_); }
    void Demo() { for (int32_t s : {0, 6, 8, 11}) { g_[0][s] = true; } for (int32_t s : {4, 12}) { g_[1][s] = true; } for (int32_t s = 0; s < kSteps; s += 2) { g_[2][s] = true; } for (int32_t s : {3, 10, 15}) { g_[3][s] = true; } }
    void Hit() {
        if (g_[0][step_]) { Beep(a_, 62U, 90U, 420U, W::kSine); }
        if (g_[1][step_]) { Beep(a_, 1800U, 80U, 240U, W::kNoise); }
        if (g_[2][step_]) { Beep(a_, 7000U, 22U, 110U, W::kNoise); }
        if (g_[3][step_]) { Beep(a_, 330U + static_cast<uint32_t>(step_) * 33U, 60U, 170U, W::kSquare); }
    }
    void Draw() {
        c_.Begin();
        for (int32_t b = 0; b < 4; ++b) { c_.Round(b * 74 + 3, 5, 68, 36, 7, b == 0 && playing_ ? Rgb(90, 200, 120) : Rgb(52, 54, 80)); c_.Text(b * 74 + 37, b == 0 || b == 3 ? 15 : 12, b == 0 && playing_ ? "STOP" : kBtn[b], kWhite, b == 0 || b == 3 ? kSmall : kMedium, true); }
        for (int32_t t = 0; t < kTracks; ++t) { for (int32_t s = 0; s < kSteps; ++s) { const int32_t x = kX + s * kCW, y = kY + t * kCH; const Color off = s / 4 % 2 == 0 ? Rgb(40, 42, 62) : Rgb(32, 34, 50); c_.Round(x + 1, y + 1, kCW - 2, kCH - 2, 3, g_[t][s] ? (s == step_ ? kWhite : kK[t]) : (s == step_ ? Rgb(90, 94, 130) : off)); } }
        c_.Text(4, 212, "BPM", kGrey); c_.Number(38, 212, static_cast<int32_t>(bpm_), kWhite);
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; Millis ck_; bool g_[kTracks][kSteps]{}, playing_{}; int32_t step_{-1}; uint32_t bpm_{112U}, next_{};
};
}  // namespace
PORTKIT_MAIN(Game)
