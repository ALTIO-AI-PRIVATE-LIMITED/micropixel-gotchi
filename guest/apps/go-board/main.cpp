// SPDX-License-Identifier: Apache-2.0
//
// Board Buddies: three in a row, drop four and flip field against the Gotchi. BOOT for the menu.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(28, 38, 58)) {}
    void Tick(uint32_t) { if (wait_ > 0 && --wait_ == 0) { Cpu(); } Draw(); }
    void Press(int32_t x, int32_t y) {
        if (mode_ == 0) { if (y > 56) { Open(m::Clamp((y - 56) / 60, 0, 2) + 1); } return; }
        if (over_) { Open(mode_); return; } if (wait_ > 0) { return; }
        if (mode_ == 1) { const int32_t cx = (x - 58) / 60, cy = (y - 30) / 60; if (cx >= 0 && cx < 3 && cy >= 0 && cy < 3 && g_[cy][cx] == 0) { g_[cy][cx] = 1; After(); } }
        else if (mode_ == 2) { const int32_t cx = (x - 8) / 40; if (cx >= 0 && cx < 7 && g_[0][cx] == 0) { Drop(cx, 1); After(); } }
        else { const int32_t cx = (x - 48) / 25, cy = (y - 16) / 25; if (cx >= 0 && cx < 8 && cy >= 0 && cy < 8 && Flip(cx, cy, 1, true) > 0) { Beep(a_, 500U, 30U); After(); } }
    }
    void Button(KeyCode k) { if (k == kBoot) { mode_ = 0; } }

   private:
    void Open(int32_t m0) { mode_ = m0; for (auto& r : g_) { for (auto& v : r) { v = 0; } } over_ = false; wait_ = 0; msg_ = "Your turn"; if (m0 == 3) { g_[3][3] = g_[4][4] = 1; g_[3][4] = g_[4][3] = 2; } }
    int32_t Drop(int32_t x, int32_t who) { for (int32_t y = 5; y >= 0; --y) { if (g_[y][x] == 0) { g_[y][x] = who; return y; } } return -1; }
    int32_t Flip(int32_t x, int32_t y, int32_t who, bool apply) {
        if (g_[y][x] != 0) { return 0; } int32_t total = 0; const int32_t other = 3 - who;
        for (int32_t dy = -1; dy <= 1; ++dy) { for (int32_t dx = -1; dx <= 1; ++dx) { if (dx == 0 && dy == 0) { continue; } int32_t cx = x + dx, cy = y + dy, n = 0; while (cx >= 0 && cy >= 0 && cx < 8 && cy < 8 && g_[cy][cx] == other) { cx += dx; cy += dy; ++n; } if (n > 0 && cx >= 0 && cy >= 0 && cx < 8 && cy < 8 && g_[cy][cx] == who) { total += n; if (apply) { int32_t ux = x + dx, uy = y + dy; for (int32_t i = 0; i < n; ++i) { g_[uy][ux] = who; ux += dx; uy += dy; } } } } }
        if (apply && total > 0) { g_[y][x] = who; } return total;
    }
    int32_t Line(int32_t who, int32_t need, int32_t w, int32_t h) const { for (int32_t y = 0; y < h; ++y) { for (int32_t x = 0; x < w; ++x) { if (g_[y][x] != who) { continue; } constexpr int32_t ds[4][2] = {{1,0},{0,1},{1,1},{1,-1}}; for (const auto& d : ds) { int32_t n = 1, cx = x + d[0], cy = y + d[1]; while (cx >= 0 && cy >= 0 && cx < w && cy < h && g_[cy][cx] == who) { ++n; cx += d[0]; cy += d[1]; } if (n >= need) { return 1; } } } } return 0; }
    bool Finished() {
        if (mode_ == 1 || mode_ == 2) { const int32_t w = mode_ == 1 ? 3 : 7, h = mode_ == 1 ? 3 : 6, need = mode_ == 1 ? 3 : 4; if (Line(1, need, w, h) != 0) { End("You win"); return true; } if (Line(2, need, w, h) != 0) { End("Gotchi wins"); return true; } for (int32_t y = 0; y < h; ++y) { for (int32_t x = 0; x < w; ++x) { if (g_[y][x] == 0) { return false; } } } End("Draw"); return true; }
        return false;
    }
    void End(const char* m0) { msg_ = m0; over_ = true; Beep(a_, 660U, 200U); }
    void After() { Beep(a_, 500U, 30U); if (!Finished()) { wait_ = 12; msg_ = "Thinking"; } }
    bool CanMove(int32_t who) { for (int32_t y = 0; y < 8; ++y) { for (int32_t x = 0; x < 8; ++x) { if (Flip(x, y, who, false) > 0) { return true; } } } return false; }
    void FlipEnd() { if (CanMove(1) || CanMove(2)) { msg_ = "Your turn"; return; } int32_t a = 0, b = 0; for (auto& r : g_) { for (int32_t v : r) { a += v == 1; b += v == 2; } } End(a == b ? "Draw" : (a > b ? "You win" : "Gotchi wins")); }
    void Cpu() {
        msg_ = "Your turn";
        if (mode_ == 1) {
            for (int32_t who : {2, 1}) { for (int32_t y = 0; y < 3; ++y) { for (int32_t x = 0; x < 3; ++x) { if (g_[y][x] == 0) { g_[y][x] = who; const bool w = Line(who, 3, 3, 3) != 0; g_[y][x] = 0; if (w) { g_[y][x] = 2; Finished(); return; } } } } }
            if (g_[1][1] == 0) { g_[1][1] = 2; Finished(); return; }
            for (;;) { const auto x = static_cast<int32_t>(a_.random().Below(3U)), y = static_cast<int32_t>(a_.random().Below(3U)); if (g_[y][x] == 0) { g_[y][x] = 2; Finished(); return; } }
        }
        if (mode_ == 2) {
            for (int32_t who : {2, 1}) { for (int32_t x = 0; x < 7; ++x) { if (g_[0][x] != 0) { continue; } const int32_t y = Drop(x, who); const bool w = Line(who, 4, 7, 6) != 0; g_[y][x] = 0; if (w) { Drop(x, 2); Finished(); return; } } }
            for (int32_t tries = 0; tries < 60; ++tries) { const int32_t x = m::Clamp(3 + static_cast<int32_t>(a_.random().Below(7U)) - 3, 0, 6); if (g_[0][x] == 0) { Drop(x, 2); break; } }
            Finished(); return;
        }
        int32_t best = 0, bx = -1, by = -1; for (int32_t y = 0; y < 8; ++y) { for (int32_t x = 0; x < 8; ++x) { int32_t v = Flip(x, y, 2, false); if (v > 0) { if ((x == 0 || x == 7) && (y == 0 || y == 7)) { v += 20; } if (v > best) { best = v; bx = x; by = y; } } } }
        if (bx >= 0) { (void)Flip(bx, by, 2, true); }
        if (!CanMove(1) && CanMove(2)) { wait_ = 12; return; }
        FlipEnd();
    }
    void Draw() {
        c_.Begin();
        if (mode_ == 0) { c_.Text(kW / 2, 14, "BOARD BUDDIES", Rgb(255, 214, 110), kLarge, true); const char* const n[] = {"Three in a Row","Drop Four","Flip Field"}; for (int32_t i = 0; i < 3; ++i) { c_.Round(30, 58 + i * 60, kW - 60, 50, 10, Rgb(50, 70, 110)); c_.Text(kW / 2, 72 + i * 60, n[i], kWhite, kMedium, true); } }
        else if (mode_ == 1) { for (int32_t y = 0; y < 3; ++y) { for (int32_t x = 0; x < 3; ++x) { const int32_t px = 58 + x * 60, py = 30 + y * 60; c_.Round(px + 2, py + 2, 56, 56, 6, Rgb(50, 70, 110)); if (g_[y][x] == 1) { c_.Disc(px + 30, py + 30, 20, Rgb(120, 230, 255)); c_.Disc(px + 30, py + 30, 12, Rgb(50, 70, 110)); } else if (g_[y][x] == 2) { c_.Line(px + 14, py + 14, px + 46, py + 46, Rgb(255, 130, 110), 6); c_.Line(px + 46, py + 14, px + 14, py + 46, Rgb(255, 130, 110), 6); } } } c_.Text(kW / 2, 6, msg_, kWhite, kMedium, true); }
        else if (mode_ == 2) { c_.Round(6, 30, 284, 204, 8, Rgb(30, 70, 170)); for (int32_t y = 0; y < 6; ++y) { for (int32_t x = 0; x < 7; ++x) { const Color k = g_[y][x] == 1 ? Rgb(255, 214, 90) : (g_[y][x] == 2 ? Rgb(240, 70, 80) : Rgb(14, 20, 40)); c_.Disc(28 + x * 40, 48 + y * 33, 14, k); } } c_.Text(kW / 2, 6, msg_, kWhite, kMedium, true); }
        else { c_.Fill(48, 16, 200, 200, Rgb(30, 110, 70)); for (int32_t i = 0; i <= 8; ++i) { c_.Fill(48 + i * 25, 16, 1, 200, Rgb(10, 60, 40)); c_.Fill(48, 16 + i * 25, 200, 1, Rgb(10, 60, 40)); } int32_t a = 0, b = 0; for (int32_t y = 0; y < 8; ++y) { for (int32_t x = 0; x < 8; ++x) { if (g_[y][x] != 0) { c_.Disc(60 + x * 25, 28 + y * 25, 9, g_[y][x] == 1 ? kWhite : Rgb(24, 24, 34)); a += g_[y][x] == 1; b += g_[y][x] == 2; } } } c_.Number(24, 60, a, kWhite, kMedium, true); c_.Number(272, 60, b, kGrey, kMedium, true); c_.Text(kW / 2, 220, msg_, kWhite, kSmall, true); }
        if (over_) { Banner(c_, msg_, "Tap to play again", Rgb(255, 214, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t g_[8][8]{}, mode_{}, wait_{}; bool over_{}; const char* msg_{""};
};
}  // namespace
PORTKIT_MAIN(Game)
