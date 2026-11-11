// SPDX-License-Identifier: Apache-2.0
//
// Lap Clock: a stopwatch with laps, and an interval timer for workouts.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 18, 26)), ck_(a) {}
    void Tick(uint32_t) { const uint32_t now = ck_.Now(); const uint32_t d = now - last_; last_ = now; if (tab_ == 0 && run_) { el_ += d; } if (tab_ == 1 && irun_) { left_ = left_ > d ? left_ - d : 0U; if (left_ == 0U) { Flip(); } } Draw(); }
    void Press(int32_t x, int32_t y) { if (y < 40) { tab_ = x > kW / 2 ? 1 : 0; return; } if (y > 184) { if (x < kW / 2) { Left(); } else { Right(); } return; } if (tab_ == 1) { Adj(y < 112 ? (x < kW / 2 ? -5 : 5) : 0, y >= 112 ? (x < kW / 2 ? -5 : 5) : 0); } }
    void Button(KeyCode k) { if (k == kBoot) { Left(); } else if (k == kVolUp) { Right(); } }

   private:
    void Left() { if (tab_ == 0) { run_ = !run_; Beep(a_, 700U, 30U); } else { irun_ = !irun_; if (irun_ && left_ == 0U) { work_ = true; left_ = static_cast<uint32_t>(ws_) * 1000U; rounds_ = 0; } } }
    void Right() { if (tab_ == 0) { if (run_) { for (int32_t i = 3; i > 0; --i) { lap_[i] = lap_[i - 1]; } lap_[0] = el_; ++laps_; Beep(a_, 900U, 30U); } else { el_ = 0U; laps_ = 0; for (auto& l : lap_) { l = 0U; } } } else { irun_ = false; left_ = 0U; rounds_ = 0; work_ = true; } }
    void Adj(int32_t a, int32_t b) { ws_ = m::Clamp(ws_ + a, 5, 300); rs_ = m::Clamp(rs_ + b, 5, 300); }
    void Flip() { work_ = !work_; if (work_) { ++rounds_; } left_ = static_cast<uint32_t>(work_ ? ws_ : rs_) * 1000U; Beep(a_, work_ ? 1000U : 600U, 200U, 300U); }
    static void Time(char* s, uint32_t ms) { const uint32_t m0 = ms / 60000U, sec = ms / 1000U % 60U, cs = ms / 10U % 100U; s[0] = static_cast<char>('0' + m0 / 10U % 10U); s[1] = static_cast<char>('0' + m0 % 10U); s[2] = ':'; s[3] = static_cast<char>('0' + sec / 10U); s[4] = static_cast<char>('0' + sec % 10U); s[5] = '.'; s[6] = static_cast<char>('0' + cs / 10U); s[7] = static_cast<char>('0' + cs % 10U); s[8] = '\0'; }
    void Draw() {
        c_.Begin(); for (int32_t t = 0; t < 2; ++t) { c_.Round(4 + t * 146, 4, 142, 32, 8, t == tab_ ? Rgb(90, 170, 255) : Rgb(40, 50, 70)); c_.Text(75 + t * 146, 11, t == 0 ? "Stopwatch" : "Intervals", kWhite, kSmall, true); }
        char s[10];
        if (tab_ == 0) { Time(s, el_); c_.Text(kW / 2, 56, s, kWhite, kTitle, true); for (int32_t i = 0; i < 4 && i < laps_; ++i) { Time(s, lap_[i]); c_.Number(50, 106 + i * 19, laps_ - i, kGrey, kSmall, true); c_.Text(180, 106 + i * 19, s, kWhite, kSmall, true); } c_.Round(6, 190, 138, 44, 10, run_ ? Rgb(200, 70, 80) : Rgb(70, 170, 110)); c_.Text(75, 204, run_ ? "Stop" : "Start", kWhite, kSmall, true); c_.Round(152, 190, 138, 44, 10, Rgb(60, 70, 96)); c_.Text(221, 204, run_ ? "Lap" : "Reset", kWhite, kSmall, true); }
        else { c_.Text(kW / 2, 44, "Work seconds, tap - or +", kGrey, kSmall, true); c_.Number(kW / 2, 62, ws_, irun_ && work_ ? Rgb(255, 170, 90) : kWhite, kLarge, true); c_.Text(kW / 2, 104, "Rest seconds", kGrey, kSmall, true); c_.Number(kW / 2, 122, rs_, irun_ && !work_ ? Rgb(120, 230, 170) : kWhite, kLarge, true); Time(s, left_); c_.Text(70, 164, irun_ ? (work_ ? "WORK" : "REST") : "Ready", kWhite, kSmall, true); c_.Text(170, 164, s, kWhite, kSmall, true); c_.Number(260, 164, rounds_, kGrey, kSmall, true); c_.Round(6, 190, 138, 44, 10, irun_ ? Rgb(200, 70, 80) : Rgb(70, 170, 110)); c_.Text(75, 204, irun_ ? "Pause" : "Start", kWhite, kSmall, true); c_.Round(152, 190, 138, 44, 10, Rgb(60, 70, 96)); c_.Text(221, 204, "Reset", kWhite, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; Millis ck_; uint32_t last_{}, el_{}, left_{}, lap_[4]{}; int32_t tab_{}, laps_{}, ws_{30}, rs_{10}, rounds_{}; bool run_{}, irun_{}, work_{true};
};
}  // namespace
PORTKIT_MAIN(Game)
