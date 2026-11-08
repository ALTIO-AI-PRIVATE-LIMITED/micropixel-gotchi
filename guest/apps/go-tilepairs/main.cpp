// SPDX-License-Identifier: Apache-2.0
//
// Tile Pairs: clear a two-layer table by matching free tiles. Every deal can be solved.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kBC = 8, kBR = 4, kT = kBC * kBR + 8, kTW = 33, kTH = 44;
constexpr Color kK[] = {Rgb(230,70,80),Rgb(60,160,90),Rgb(60,110,220),Rgb(220,150,30),Rgb(150,80,200),Rgb(30,170,170),Rgb(200,90,140),Rgb(120,120,40),Rgb(90,90,120),Rgb(180,70,30)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(22, 60, 50), 1400U, 44U) { Deal(); }
    void Tick(uint32_t ms) { t_ += ms; Draw(); }
    void Press(int32_t x, int32_t y) {
        if (won_) { Deal(); return; }
        for (int32_t i = kBC * kBR; i < kT; ++i) { if (On(i, x, y)) { Pick(i); return; } }
        for (int32_t i = 0; i < kBC * kBR; ++i) { if (On(i, x, y)) { Pick(i); return; } }
    }
    void Button(KeyCode k) { if (k == kBoot) { Deal(); } }

   private:
    int32_t PX(int32_t i) const { return i < kBC * kBR ? 16 + (i % kBC) * kTW : 16 + (2 + (i - kBC * kBR) % 4) * kTW - 4; }
    int32_t PY(int32_t i) const { return i < kBC * kBR ? 30 + (i / kBC) * kTH : 30 + (1 + (i - kBC * kBR) / 4) * kTH - 4; }
    bool On(int32_t i, int32_t x, int32_t y) const { return tile_[i] > 0 && x >= PX(i) && y >= PY(i) && x < PX(i) + kTW - 2 && y < PY(i) + kTH - 2; }
    int32_t At(int32_t cx, int32_t cy) const { return cx < 0 || cy < 0 || cx >= kBC || cy >= kBR ? -1 : cx + cy * kBC; }
    bool Free(const int8_t* t, int32_t i) const {
        if (i >= kBC * kBR) { const int32_t c = (i - kBC * kBR) % 4; const bool l = c > 0 && t[i - 1] > 0, rr = c < 3 && t[i + 1] > 0; return !l || !rr; }
        const int32_t cx = i % kBC, cy = i / kBC; if (cx >= 2 && cx <= 5 && cy >= 1 && cy <= 2 && t[kBC * kBR + (cx - 2) + (cy - 1) * 4] > 0) { return false; }
        const int32_t l = At(cx - 1, cy), r = At(cx + 1, cy); return l < 0 || r < 0 || t[l] == 0 || t[r] == 0;
    }
    void Deal() {
        for (int32_t attempt = 0; attempt < 200; ++attempt) {
            int8_t live[kT]; for (auto& v : live) { v = 1; } int32_t left = kT; bool stuck = false;
            while (left > 0) { int32_t fr[kT], n = 0; for (int32_t i = 0; i < kT; ++i) { if (live[i] != 0 && Free(live, i)) { fr[n++] = i; } } if (n < 2) { stuck = true; break; } const auto a = static_cast<int32_t>(a_.random().Below(static_cast<uint32_t>(n))); auto b = static_cast<int32_t>(a_.random().Below(static_cast<uint32_t>(n - 1))); if (b >= a) { ++b; } const int32_t s = 1 + static_cast<int32_t>(a_.random().Below(20U)); tile_[fr[a]] = static_cast<int8_t>(s); tile_[fr[b]] = static_cast<int8_t>(s); live[fr[a]] = live[fr[b]] = 0; left -= 2; }
            if (!stuck) { break; }
        }
        sel_ = -1; won_ = false;
    }
    void Pick(int32_t i) { if (!Free(tile_, i)) { Beep(a_, 200U, 40U); return; } if (sel_ < 0) { sel_ = i; Beep(a_, 700U, 25U, 110U); return; } if (sel_ == i) { sel_ = -1; return; } if (tile_[sel_] == tile_[i]) { tile_[sel_] = tile_[i] = 0; sel_ = -1; Beep(a_, 920U, 80U); bool any = false; for (int32_t v = 0; v < kT; ++v) { any = any || tile_[v] > 0; } won_ = !any; } else { sel_ = i; } }
    void Tile(int32_t i, bool top) { const int32_t x = PX(i), y = PY(i), v = tile_[i] - 1; const bool f = Free(tile_, i); c_.Round(x, y, kTW - 2, kTH - 2, 4, top ? Rgb(255, 250, 235) : Rgb(236, 230, 212)); if (i == sel_) { c_.Frame(x, y, kTW - 2, kTH - 2, Rgb(255, 170, 40), 2); } const Color k = kK[v % 10]; const int32_t cx = x + 15, cy = y + 20; if (v % 2 == 0) { c_.Disc(cx, cy, 10, k); } else { c_.Fill(cx - 10, cy - 10, 20, 20, k); } const char s[2] = {static_cast<char>('A' + v / 2), '\0'}; c_.Text(cx, cy - 7, s, kWhite, kSmall, true); if (!f) { c_.Fill(x, y, kTW - 2, kTH - 2, Rgb(0, 0, 0), 70U); } }
    void Draw() { c_.Begin(); for (int32_t i = 0; i < kBC * kBR; ++i) { if (tile_[i] > 0) { Tile(i, false); } } for (int32_t i = kBC * kBR; i < kT; ++i) { if (tile_[i] > 0) { Tile(i, true); } } c_.Text(10, 6, "Tap free tiles in pairs", kGrey); if (won_) { Banner(c_, "CLEAR", "Tap for a new table", Rgb(255, 214, 110)); } c_.Present(); }
    Application& a_; Canvas c_; int8_t tile_[kT]{}; int32_t sel_{-1}; uint32_t t_{}; bool won_{};
};
}  // namespace
PORTKIT_MAIN(Game)
