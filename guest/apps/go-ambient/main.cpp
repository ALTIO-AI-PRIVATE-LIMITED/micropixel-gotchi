// SPDX-License-Identifier: Apache-2.0
//
// Calm Sounds: rain, ocean, wind and fire made live on the Gotchi, with a sleep timer.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kN[] = {"Rain","Ocean","Wind","Fire"}; constexpr Color kC[] = {Rgb(90,150,240),Rgb(60,180,200),Rgb(150,170,200),Rgb(255,130,60)}; constexpr int32_t kTimers[] = {0,15,30,60};
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(12, 16, 30)), spk_(a), ck_(a), p_(a) { mode_ = static_cast<int32_t>(p_.Get("m")) % 4; vol_ = static_cast<int32_t>(p_.Get("v", 6U)); }
    Speaker& speaker() { return spk_; }
    void Tick(uint32_t) { if (on_) { Pump(); if (end_ != 0U && ck_.Now() >= end_) { Stop(); } } ++f_; Draw(); }
    void Feed() { Pump(); }
    void Press(int32_t x, int32_t y) { if (y < 118) { mode_ = m::Clamp(x / 74, 0, 3); p_.Set("m", static_cast<uint32_t>(mode_)); if (!on_) { Start(); } } else if (y < 174) { if (x < 148) { if (on_) { Stop(); } else { Start(); } } else { t_ = (t_ + 1) % 4; Arm(); } } else { vol_ = m::Clamp(x / 26, 1, 10); p_.Set("v", static_cast<uint32_t>(vol_)); } }
    void Button(KeyCode k) { if (k == kBoot) { if (on_) { Stop(); } else { Start(); } } else { vol_ = m::Clamp(vol_ + (k == kVolUp ? 1 : -1), 1, 10); p_.Set("v", static_cast<uint32_t>(vol_)); } }

   private:
    void Arm() { end_ = t_ == 0 ? 0U : ck_.Now() + static_cast<uint32_t>(kTimers[t_]) * 60000U; }
    void Start() { if (spk_.Open()) { on_ = true; Arm(); Pump(); } }
    void Stop() { spk_.Close(); on_ = false; end_ = 0U; }
    int32_t Rnd() { r_ ^= r_ << 13U; r_ ^= r_ >> 17U; r_ ^= r_ << 5U; return static_cast<int32_t>(r_ & 0xFFFFU) - 32768; }
    int16_t Sample() {
        const auto n = static_cast<float>(Rnd()); ph_ += 0.000018F; if (ph_ > m::kTwoPi) { ph_ -= m::kTwoPi; } float v = 0.0F;
        if (mode_ == 0) { lp_ += (n - lp_) * 0.55F; hp_ += (lp_ - hp_) * 0.03F; v = (lp_ - hp_) * 0.34F; }
        else if (mode_ == 1) { lp_ += (n - lp_) * 0.04F; v = lp_ * (0.75F + m::Sin(ph_ * 12.0F) * 0.65F) * 1.1F; }
        else if (mode_ == 2) { lp_ += (n - lp_) * (0.03F + (m::Sin(ph_ * 20.0F) + 1.0F) * 0.03F); v = lp_ * 1.2F; }
        else { lp_ += (n - lp_) * 0.03F; v = lp_ * 0.9F; if (crack_ > 0) { --crack_; v += n * 0.5F; } else if (Rnd() > 32600) { crack_ = 60 + (Rnd() & 200); } }
        return static_cast<int16_t>(m::Clamp(v * static_cast<float>(vol_) * 0.1F, -30000.0F, 30000.0F));
    }
    void Pump() { for (int32_t g = 0; g < 8 && on_; ++g) { if (pendingN_ == 0U) { for (auto& s : buf_) { s = Sample(); } pendingN_ = 256U; pendingAt_ = 0U; } const uint32_t took = spk_.Write(buf_ + pendingAt_, pendingN_); pendingAt_ += took; pendingN_ -= took; if (pendingN_ != 0U || took == 0U) { return; } } }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 4; ++i) { c_.Round(3 + i * 74, 8, 68, 104, 10, i == mode_ ? kC[i] : Rgb(34, 40, 62)); c_.Text(37 + i * 74, 84, kN[i], kWhite, kSmall, true); Pic(i, 37 + i * 74, 46); }
        c_.Round(6, 122, 138, 48, 10, on_ ? Rgb(200, 70, 90) : Rgb(70, 170, 110)); c_.Text(75, 138, on_ ? "Stop" : "Play", kWhite, kSmall, true);
        c_.Round(152, 122, 138, 48, 10, Rgb(50, 58, 90)); char s[16]; if (t_ == 0) { s[0] = 'N'; s[1] = 'o'; s[2] = ' '; s[3] = 't'; s[4] = 'i'; s[5] = 'm'; s[6] = 'e'; s[7] = 'r'; s[8] = '\0'; } else { Canvas::Format(s, kTimers[t_]); int32_t n = 0; while (s[n] != '\0') { ++n; } s[n++] = ' '; s[n++] = 'm'; s[n++] = 'i'; s[n++] = 'n'; s[n] = '\0'; } c_.Text(221, 138, s, kWhite, kSmall, true);
        c_.Text(10, 182, "Volume", kGrey); for (int32_t i = 1; i <= 10; ++i) { c_.Fill(i * 26 + 2, 208, 22, 22, i <= vol_ ? kC[mode_] : Rgb(34, 40, 62)); }
        c_.Present();
    }
    void Pic(int32_t i, int32_t cx, int32_t cy) {
        const int32_t t = on_ ? f_ : 0;
        if (i == 0) { c_.Disc(cx, cy - 8, 16, kWhite); for (int32_t k = 0; k < 6; ++k) { c_.Fill(cx - 15 + k * 6, cy + 8 + (t * 2 + k * 9) % 20, 2, 6, kWhite); } }
        else if (i == 1) { for (int32_t r = 0; r < 3; ++r) { for (int32_t x = -24; x <= 24; x += 2) { c_.Fill(cx + x, cy - 12 + r * 14 + static_cast<int32_t>(m::Sin(static_cast<float>(x + t) * 0.2F) * 4.0F), 2, 3, kWhite); } } }
        else if (i == 2) { for (int32_t r = 0; r < 3; ++r) { const int32_t w = 30 + r * 8; c_.Fill(cx - w / 2 + (t + r * 7) % 8, cy - 12 + r * 12, w, 3, kWhite); } }
        else { for (int32_t k = 0; k < 5; ++k) { const int32_t h = 16 + (t * (k + 1) + k * 7) % 14; c_.Round(cx - 16 + k * 7, cy + 20 - h, 6, h, 2, kWhite); } }
    }
    Canvas c_; Speaker spk_; Millis ck_; Prefs p_; int16_t buf_[256]{}; uint32_t pendingN_{}, pendingAt_{}, end_{}, r_{0x9E3779B9U}; float lp_{}, hp_{}, ph_{}; int32_t mode_{}, vol_{6}, t_{}, f_{}, crack_{}; bool on_{};
};
}  // namespace
PORTKIT_MAIN(Game)
