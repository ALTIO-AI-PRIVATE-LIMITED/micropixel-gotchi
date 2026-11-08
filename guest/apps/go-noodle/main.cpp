// SPDX-License-Identifier: Apache-2.0
//
// Noodle: steer a growing noodle to the dumplings. Swipe to turn, or use the volume keys.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
constexpr int32_t kCell = 14, kCols = 21, kRows = 15, kTop = 30, kMax = kCols * kRows;
constexpr int8_t kDx[] = {1, 0, -1, 0}, kDy[] = {0, 1, 0, -1};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 26, 26)), p_(a) { best_ = p_.Get("best"); }
    void Tick(uint32_t ms) { if (state_ == 1) { el_ += ms; const uint32_t gap = len_ > 40 ? 90U : 180U - static_cast<uint32_t>(len_) * 2U; if (el_ >= gap) { el_ = 0U; Step(); } } Draw(); }
    void Press(int32_t x, int32_t y) { sx_ = x; sy_ = y; used_ = false; }
    void Drag(int32_t x, int32_t y) {
        if (used_) { return; }
        const int32_t dx = x - sx_, dy = y - sy_, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax < 14 && ay < 14) { return; }
        used_ = true; Want(ax > ay ? (dx > 0 ? 0 : 2) : (dy > 0 ? 1 : 3));
    }
    void Tap(int32_t, int32_t) { if (state_ != 1) { Start(); } }
    void Button(KeyCode k) { if (state_ != 1) { Start(); return; } if (k == kVolUp) { want_ = (dir_ + 1) % 4; } else if (k == kVolDown) { want_ = (dir_ + 3) % 4; } }

   private:
    void Want(int32_t d) { if ((d + 2) % 4 != dir_) { want_ = d; } }
    void Start() { len_ = 4; dir_ = want_ = 0; head_ = len_ - 1; for (int32_t i = 0; i < len_; ++i) { xs_[i] = static_cast<uint8_t>(6 + i); ys_[i] = 7U; } state_ = 1; Food(); }
    bool Hit(int32_t x, int32_t y) const { for (int32_t i = 0; i < len_; ++i) { const int32_t k = (head_ - i + kMax * 2) % kMax; if (xs_[k] == x && ys_[k] == y) { return true; } } return false; }
    void Food() { do { fx_ = static_cast<int32_t>(a_.random().Below(kCols)); fy_ = static_cast<int32_t>(a_.random().Below(kRows)); } while (Hit(fx_, fy_)); }
    void Step() {
        dir_ = want_;
        const int32_t x = xs_[head_] + kDx[dir_], y = ys_[head_] + kDy[dir_];
        if (x < 0 || y < 0 || x >= kCols || y >= kRows || Hit(x, y)) { state_ = 2; Beep(a_, 120U, 300U, 220U, micropixel::Waveform::kSquare); if (score() > best_) { best_ = score(); p_.Set("best", best_); } return; }
        head_ = (head_ + 1) % kMax; xs_[head_] = static_cast<uint8_t>(x); ys_[head_] = static_cast<uint8_t>(y);
        if (x == fx_ && y == fy_) { if (len_ < kMax - 1) { ++len_; } Beep(a_, 720U, 50U); Food(); }
    }
    uint32_t score() const { return static_cast<uint32_t>(len_ - 4) * 10U; }
    void Draw() {
        c_.Begin();
        c_.Fill(2, kTop, kCols * kCell + 2, kRows * kCell, Rgb(28, 44, 42));
        c_.Disc(3 + fx_ * kCell + 7, kTop + fy_ * kCell + 7, 6, Rgb(255, 214, 150));
        if (state_ != 0) { for (int32_t i = len_ - 1; i >= 0; --i) { const int32_t k = (head_ - i + kMax * 2) % kMax; c_.Round(3 + xs_[k] * kCell, kTop + ys_[k] * kCell, kCell, kCell, 4, i == 0 ? Rgb(255, 150, 90) : Rgb(250, 210 - (i % 3) * 18, 110)); } }
        c_.Text(8, 8, "SCORE", kGrey); c_.Number(58, 8, static_cast<int32_t>(score()), kWhite);
        c_.Text(208, 8, "BEST", kGrey); c_.Number(250, 8, static_cast<int32_t>(best_), kWhite);
        if (state_ == 0) { Banner(c_, "NOODLE", "Swipe to steer, tap to start", Rgb(255, 200, 110)); }
        if (state_ == 2) { Banner(c_, "TANGLED", "Tap to try again", Rgb(255, 130, 100)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_;
    uint8_t xs_[kMax]{}, ys_[kMax]{}; int32_t len_{}, head_{}, dir_{}, want_{}, fx_{}, fy_{}, state_{}, sx_{}, sy_{}; uint32_t el_{}, best_{}; bool used_{};
};
}  // namespace
PORTKIT_MAIN(Game)
