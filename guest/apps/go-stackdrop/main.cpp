// SPDX-License-Identifier: Apache-2.0
//
// Stack Drop: falling shapes of three and four cells. Tap the left or right side to move, the middle
// to turn, swipe down to drop. Volume keys move and BOOT turns.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
constexpr int32_t kCols = 10, kRows = 20, kCell = 11, kLeft = 92;
struct P { int8_t x[4], y[4]; uint8_t n; };
constexpr P kShapes[] = {{{-1,0,1,0},{0,0,0,0},3},{{0,1,0,0},{0,0,1,0},3},{{-1,0,1,2},{0,0,0,0},4},{{0,1,0,1},{0,0,1,1},4},
                         {{-1,0,1,0},{0,0,0,1},4},{{-1,0,1,1},{0,0,0,1},4},{{-1,0,0,1},{1,1,0,0},4},{{-1,0,0,1},{0,0,1,1},4}};
constexpr Color kCols2[] = {Rgb(255,118,96),Rgb(255,190,80),Rgb(110,220,255),Rgb(255,230,110),Rgb(190,130,255),Rgb(120,230,150),Rgb(255,120,190),Rgb(120,150,255)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 16, 30)), p_(a) { best_ = p_.Get("best"); nxt_ = Pick(); }
    void Tick(uint32_t ms) {
        if (state_ == 1) { fall_ += ms; if (fall_ >= speed()) { fall_ = 0U; Down(); } }
        if (flash_ > 0) { --flash_; }
        Draw();
    }
    void Press(int32_t x, int32_t y) { sx_ = x; sy_ = y; moved_ = false; }
    void Drag(int32_t, int32_t y) { if (state_ == 1 && !moved_ && y - sy_ > 34) { moved_ = true; while (Fits(px_, py_ + 1, rot_)) { ++py_; } Lock(); } }
    void Tap(int32_t x, int32_t) {
        if (moved_) { return; }
        if (state_ != 1) { Start(); return; }
        if (sx_ < 98) { Move(-1); } else if (sx_ > 198) { Move(1); } else { Turn(); } (void)x;
    }
    void Button(KeyCode k) { if (state_ != 1) { Start(); return; } if (k == kVolDown) { Move(-1); } else if (k == kVolUp) { Move(1); } else if (k == kBoot) { Turn(); } }

   private:
    uint8_t Pick() { return static_cast<uint8_t>(a_.random().Below(8U)); }
    uint32_t speed() const { const uint32_t d = lines_ * 18U; return d > 560U ? 140U : 700U - d; }
    void Start() { for (auto& r : g_) { for (auto& v : r) { v = 0U; } } score_ = 0U; lines_ = 0U; state_ = 1; Spawn(); }
    void Cell(int32_t i, int32_t r, int32_t& x, int32_t& y) const { const P& s = kShapes[cur_]; int32_t a = s.x[i], b = s.y[i]; for (int32_t k = 0; k < r; ++k) { const int32_t t = a; a = -b; b = t; } x = a; y = b; }
    bool Fits(int32_t px, int32_t py, int32_t r) const { for (int32_t i = 0; i < kShapes[cur_].n; ++i) { int32_t x, y; Cell(i, r, x, y); x += px; y += py; if (x < 0 || x >= kCols || y >= kRows) { return false; } if (y >= 0 && g_[y][x] != 0U) { return false; } } return true; }
    void Spawn() { cur_ = nxt_; nxt_ = Pick(); px_ = 4; py_ = 0; rot_ = 0; if (!Fits(px_, py_, rot_)) { state_ = 2; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } Beep(a_, 140U, 300U); } }
    void Move(int32_t d) { if (Fits(px_ + d, py_, rot_)) { px_ += d; } }
    void Turn() { const int32_t r = (rot_ + 1) % 4; for (int32_t k : {0, -1, 1}) { if (Fits(px_ + k, py_, r)) { px_ += k; rot_ = r; Beep(a_, 620U, 25U, 120U); return; } } }
    void Down() { if (Fits(px_, py_ + 1, rot_)) { ++py_; } else { Lock(); } }
    void Lock() {
        for (int32_t i = 0; i < kShapes[cur_].n; ++i) { int32_t x, y; Cell(i, rot_, x, y); x += px_; y += py_; if (y >= 0) { g_[y][x] = static_cast<uint8_t>(cur_ + 1); } }
        int32_t n = 0;
        for (int32_t r = kRows - 1; r >= 0; --r) {
            bool full = true; for (int32_t x = 0; x < kCols; ++x) { full = full && g_[r][x] != 0U; }
            if (full) { ++n; for (int32_t q = r; q > 0; --q) { for (int32_t x = 0; x < kCols; ++x) { g_[q][x] = g_[q - 1][x]; } } for (auto& v : g_[0]) { v = 0U; } ++r; }
        }
        if (n > 0) { lines_ += static_cast<uint32_t>(n); score_ += static_cast<uint32_t>(n * n) * 100U; flash_ = 5; Beep(a_, 780U, 110U); }
        Spawn();
    }
    void Block(int32_t x, int32_t y, Color k) { c_.Fill(x, y, kCell - 1, kCell - 1, k); c_.Fill(x, y, kCell - 1, 2, k.Lightened(90U)); }
    void Draw() {
        c_.Begin();
        c_.Fill(kLeft - 3, 0, kCols * kCell + 5, kH, flash_ > 0 ? Rgb(70, 74, 110) : Rgb(26, 28, 50));
        for (int32_t y = 0; y < kRows; ++y) { for (int32_t x = 0; x < kCols; ++x) { if (g_[y][x] != 0U) { Block(kLeft + x * kCell, y * kCell + 10, kCols2[g_[y][x] - 1U]); } } }
        if (state_ == 1) { for (int32_t i = 0; i < kShapes[cur_].n; ++i) { int32_t x, y; Cell(i, rot_, x, y); x += px_; y += py_; if (y >= 0) { Block(kLeft + x * kCell, y * kCell + 10, kCols2[cur_]); } } }
        c_.Text(10, 14, "SCORE", kGrey); c_.Number(10, 32, static_cast<int32_t>(score_), kWhite, kMedium);
        c_.Text(10, 66, "BEST", kGrey); c_.Number(10, 84, static_cast<int32_t>(best_), kWhite, kMedium);
        c_.Text(10, 118, "LINES", kGrey); c_.Number(10, 136, static_cast<int32_t>(lines_), kWhite, kMedium);
        c_.Text(222, 14, "NEXT", kGrey);
        for (int32_t i = 0; i < kShapes[nxt_].n; ++i) { Block(248 + kShapes[nxt_].x[i] * kCell, 46 + kShapes[nxt_].y[i] * kCell, kCols2[nxt_]); }
        if (state_ == 0) { Banner(c_, "STACK DROP", "Tap to start", Rgb(110, 220, 255)); }
        if (state_ == 2) { Banner(c_, "TOPPED OUT", "Tap to try again", Rgb(255, 118, 96)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_;
    uint8_t g_[kRows][kCols]{}; uint8_t cur_{}, nxt_{};
    int32_t px_{}, py_{}, rot_{}, state_{}, flash_{}, sx_{}, sy_{}; uint32_t fall_{}, score_{}, lines_{}, best_{}; bool moved_{};
};
}  // namespace
PORTKIT_MAIN(Game)
