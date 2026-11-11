// SPDX-License-Identifier: Apache-2.0
//
// Mine Patrol: open the field without hitting a mine. Press BOOT or the flag button to mark.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 16, kR = 11, kS = 18, kY = 30, kMines = 26;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(30, 36, 46), 1000U, 48U) { New(); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) {
        if (state_ == 2 || state_ == 3) { New(); return; }
        if (y < kY) { if (x > 200) { flag_ = !flag_; } return; }
        const int32_t cx = (x - 4) / kS, cy = (y - kY) / kS; if (cx < 0 || cy < 0 || cx >= kC || cy >= kR) { return; }
        if (flag_) { if (!open_[cy][cx]) { mark_[cy][cx] = !mark_[cy][cx]; Beep(a_, 700U, 20U, 80U); } return; }
        if (mark_[cy][cx]) { return; }
        if (state_ == 0) { Place(cx, cy); state_ = 1; }
        Reveal(cx, cy);
    }
    void Button(KeyCode k) { if (k == kBoot) { flag_ = !flag_; } }

   private:
    void New() { for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { mine_[y][x] = open_[y][x] = mark_[y][x] = false; } } state_ = 0; flag_ = false; }
    void Place(int32_t sx, int32_t sy) { int32_t n = 0; while (n < kMines) { const auto x = static_cast<int32_t>(a_.random().Below(kC)), y = static_cast<int32_t>(a_.random().Below(kR)); if (!mine_[y][x] && (m::Abs(x - sx) > 1 || m::Abs(y - sy) > 1)) { mine_[y][x] = true; ++n; } } }
    int32_t Count(int32_t x, int32_t y) const { int32_t n = 0; for (int32_t b = -1; b <= 1; ++b) { for (int32_t a = -1; a <= 1; ++a) { const int32_t cx = x + a, cy = y + b; if (cx >= 0 && cy >= 0 && cx < kC && cy < kR && mine_[cy][cx]) { ++n; } } } return n; }
    void Reveal(int32_t sx, int32_t sy) {
        if (mine_[sy][sx]) { state_ = 3; Beep(a_, 90U, 500U, 320U, W::kNoise); return; }
        int16_t stack[kC * kR]; int32_t n = 0; stack[n++] = static_cast<int16_t>(sy * kC + sx);
        while (n > 0) { const int32_t v = stack[--n], x = v % kC, y = v / kC; if (open_[y][x]) { continue; } open_[y][x] = true; if (Count(x, y) != 0) { continue; } for (int32_t b = -1; b <= 1; ++b) { for (int32_t a = -1; a <= 1; ++a) { const int32_t cx = x + a, cy = y + b; if (cx >= 0 && cy >= 0 && cx < kC && cy < kR && !open_[cy][cx] && !mark_[cy][cx] && n < kC * kR) { stack[n++] = static_cast<int16_t>(cy * kC + cx); } } } }
        Beep(a_, 500U, 15U, 80U); int32_t left = 0; for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { left += open_[y][x] ? 0 : 1; } } if (left == kMines) { state_ = 2; Beep(a_, 880U, 260U); }
    }
    void Draw() {
        constexpr Color k[9] = {kWhite, Rgb(120,180,255), Rgb(130,230,140), Rgb(255,130,120), Rgb(190,140,255), Rgb(255,190,90), Rgb(120,230,230), kWhite, kGrey};
        c_.Begin(); int32_t flags = 0;
        for (int32_t y = 0; y < kR; ++y) { for (int32_t x = 0; x < kC; ++x) { const int32_t px = 4 + x * kS, py = kY + y * kS; if (open_[y][x]) { c_.Fill(px, py, kS - 1, kS - 1, Rgb(46, 54, 68)); const int32_t n = Count(x, y); if (n > 0) { Num(px, py, n, k[n]); } } else { c_.Fill(px, py, kS - 1, kS - 1, Rgb(96, 110, 136)); c_.Fill(px, py, kS - 1, 2, Rgb(130, 146, 172)); } if (mark_[y][x]) { c_.Fill(px + 6, py + 3, 8, 6, Rgb(255, 90, 90)); c_.Fill(px + 5, py + 3, 2, 12, kWhite); ++flags; } if (state_ == 3 && mine_[y][x]) { c_.Disc(px + 8, py + 8, 5, Rgb(255, 70, 70)); } } }
        c_.Text(8, 6, "Mines", kGrey); c_.Number(56, 6, kMines - flags, kWhite);
        c_.Round(202, 2, 90, 26, 6, flag_ ? Rgb(220, 80, 80) : Rgb(60, 70, 96)); c_.Text(247, 7, flag_ ? "Flag on" : "Flag off", kWhite, kSmall, true);
        if (state_ == 2) { Banner(c_, "FIELD CLEAR", "Tap for a new field", Rgb(130, 230, 140)); } if (state_ == 3) { Banner(c_, "BOOM", "Tap for a new field", Rgb(255, 110, 110)); }
        c_.Present();
    }
    // Pips rather than labels, so a wide-open field does not run out of text slots.
    void Num(int32_t px, int32_t py, int32_t n, Color k) { for (int32_t i = 0; i < n; ++i) { c_.Fill(px + 3 + (i % 3) * 4, py + 3 + (i / 3) * 4, 3, 3, k); } }
    Application& a_; Canvas c_; bool mine_[kR][kC]{}, open_[kR][kC]{}, mark_[kR][kC]{}, flag_{}; int32_t state_{};
};
}  // namespace
PORTKIT_MAIN(Game)
