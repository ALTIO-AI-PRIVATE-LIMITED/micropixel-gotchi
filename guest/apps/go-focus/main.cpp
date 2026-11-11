// SPDX-License-Identifier: Apache-2.0
//
// Focus Timer: work and break rounds with a ring that counts down and a chime at the end.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kPre[3][2] = {{25, 5}, {50, 10}, {15, 3}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(22, 18, 30)), p_(a), ck_(a) { done_ = p_.Get("done"); pre_ = static_cast<int32_t>(p_.Get("pre")) % 3; Load(); }
    void Tick(uint32_t) { const uint32_t now = ck_.Now(); if (run_) { const uint32_t d = now - last_; left_ = left_ > d ? left_ - d : 0U; if (left_ == 0U) { Finish(); } } last_ = now; if (bell_ > 0) { Bell(); } Draw(); }
    void Press(int32_t x, int32_t y) { if (y > 196) { if (x < 148) { pre_ = (pre_ + 1) % 3; p_.Set("pre", static_cast<uint32_t>(pre_)); brk_ = false; run_ = false; Load(); } else { run_ = false; brk_ = false; Load(); } } else { run_ = !run_; } }
    void Button(KeyCode k) { if (k == kBoot) { run_ = !run_; } else if (k == kVolUp) { left_ += 60000U; } else if (left_ > 60000U) { left_ -= 60000U; } }

   private:
    void Load() { left_ = static_cast<uint32_t>(kPre[pre_][brk_ ? 1 : 0]) * 60000U; total_ = left_; }
    void Finish() { if (!brk_) { ++done_; p_.Set("done", done_); } brk_ = !brk_; Load(); bell_ = 60; bt_ = 0; }
    void Bell() { if (bt_ % 18 == 0) { Beep(a_, brk_ ? 880U : 660U, 180U, 320U, W::kTriangle); } ++bt_; --bell_; }
    void Draw() {
        c_.Begin();
        const Color col = brk_ ? Rgb(110, 220, 170) : Rgb(255, 120, 90);
        constexpr int32_t kDots = 60; const int32_t lit = total_ == 0U ? 0 : static_cast<int32_t>(static_cast<uint64_t>(left_) * kDots / total_);
        for (int32_t i = 0; i < kDots; ++i) { const float t = static_cast<float>(i) * m::kTwoPi / kDots - m::kHalfPi; c_.Disc(148 + static_cast<int32_t>(m::Cos(t) * 88.0F), 100 + static_cast<int32_t>(m::Sin(t) * 88.0F), 3, i < lit ? col : Rgb(50, 46, 62)); }
        const uint32_t s = (left_ + 999U) / 1000U; char t[8]; Canvas::Format(t, static_cast<int32_t>(s / 60U)); int32_t n = 0; while (t[n] != '\0') { ++n; } t[n++] = ':'; t[n++] = static_cast<char>('0' + (s % 60U) / 10U); t[n++] = static_cast<char>('0' + (s % 60U) % 10U); t[n] = '\0';
        c_.Text(148, 78, t, kWhite, kTitle, true); c_.Text(148, 54, brk_ ? "Break" : "Focus", col, kSmall, true); c_.Text(148, 120, run_ ? "tap to pause" : "tap to start", kGrey, kSmall, true);
        c_.Round(6, 200, 138, 34, 8, Rgb(52, 48, 72)); char p[12]; Canvas::Format(p, kPre[pre_][0]); int32_t q = 0; while (p[q] != '\0') { ++q; } p[q++] = '/'; Canvas::Format(p + q, kPre[pre_][1]); c_.Text(75, 208, p, kWhite, kSmall, true);
        c_.Round(152, 200, 138, 34, 8, Rgb(52, 48, 72)); c_.Text(221, 208, "Reset", kWhite, kSmall, true);
        c_.Text(10, 8, "Done", kGrey); c_.Number(44, 8, static_cast<int32_t>(done_), kWhite);
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; Millis ck_; uint32_t left_{}, total_{}, last_{}, done_{}; int32_t pre_{}, bell_{}, bt_{}; bool run_{}, brk_{};
};
}  // namespace
PORTKIT_MAIN(Game)
