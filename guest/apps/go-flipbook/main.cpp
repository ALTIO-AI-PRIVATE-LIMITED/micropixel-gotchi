// SPDX-License-Identifier: Apache-2.0
//
// Flip Book: draw six frames with your finger and play them back as a little animation.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kC = 40, kR = 30, kS = 6, kF = 6, kBytes = kC * kR / 8;
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(30, 30, 40), 1300U, 12U), p_(a) { for (int32_t i = 0; i < kF; ++i) { const char k[3] = {'f', static_cast<char>('0' + i), '\0'}; (void)p_.Load(k, f_[i], kBytes); } }
    void Tick(uint32_t ms) { if (play_) { el_ += ms; if (el_ >= 180U) { el_ = 0U; show_ = (show_ + 1) % kF; } } Draw(); }
    void Press(int32_t x, int32_t y) { if (x >= kC * kS) { const int32_t b = (y - 4) / 58; if (b == 0) { Save(); cur_ = (cur_ + 1) % kF; } else if (b == 1) { Save(); cur_ = (cur_ + kF - 1) % kF; } else if (b == 2) { play_ = !play_; show_ = cur_; Save(); } else { for (auto& v : f_[cur_]) { v = 0U; } } return; } if (!play_ && y < kR * kS) { ink_ = !Get(x / kS, y / kS); Paint(x, y); } }
    void Drag(int32_t x, int32_t y) { if (!play_ && x < kC * kS && y < kR * kS) { Paint(x, y); } }
    void Tap(int32_t, int32_t) { Save(); }
    void Button(KeyCode k) { if (k == kBoot) { play_ = !play_; show_ = cur_; } }

   private:
    bool Get(int32_t x, int32_t y) const { return Bit(cur_, x, y); }
    bool Bit(int32_t fr, int32_t x, int32_t y) const { const int32_t i = y * kC + x; return (f_[fr][i / 8] >> (i % 8) & 1U) != 0U; }
    void Set(int32_t x, int32_t y, bool on) { if (x < 0 || y < 0 || x >= kC || y >= kR) { return; } const int32_t i = y * kC + x; const auto bit = static_cast<uint8_t>(1U << (i % 8)); if (on) { f_[cur_][i / 8] |= bit; } else { f_[cur_][i / 8] &= static_cast<uint8_t>(~bit); } }
    void Paint(int32_t x, int32_t y) { const int32_t cx = x / kS, cy = y / kS; for (int32_t b = 0; b < 2; ++b) { for (int32_t a = 0; a < 2; ++a) { Set(cx + a, cy + b, ink_); } } }
    void Save() { const char k[3] = {'f', static_cast<char>('0' + cur_), '\0'}; p_.Save(k, f_[cur_], kBytes); }
    void Draw() {
        c_.Begin(); c_.Fill(0, 0, kC * kS, kR * kS, Rgb(250, 248, 236));
        const int32_t fr = play_ ? show_ : cur_;
        if (!play_ && cur_ > 0) { Runs(cur_ - 1, Rgb(205, 205, 215)); }
        Runs(fr, Rgb(30, 30, 50));
        c_.Number(120, 188, fr + 1, kWhite, kMedium, true); c_.Text(144, 192, "of 6", kGrey);
        const char* const t[] = {"Next", "Back", play_ ? "Stop" : "Play", "Clear"}; for (int32_t i = 0; i < 4; ++i) { c_.Round(kC * kS + 6, 6 + i * 58, 44, 52, 8, Rgb(70, 90, 140)); c_.Text(kC * kS + 28, 24 + i * 58, t[i], kWhite, kSmall, true); }
        c_.Present();
    }
    void Runs(int32_t fr, Color k) { for (int32_t y = 0; y < kR; ++y) { int32_t s = -1; for (int32_t x = 0; x <= kC; ++x) { const bool on = x < kC && Bit(fr, x, y); if (on && s < 0) { s = x; } if (!on && s >= 0) { c_.Fill(s * kS, y * kS, (x - s) * kS, kS, k); s = -1; } } } }
    Canvas c_; Prefs p_; uint8_t f_[kF][kBytes]{}; int32_t cur_{}, show_{}; uint32_t el_{}; bool play_{}, ink_{true};
};
}  // namespace
PORTKIT_MAIN(Game)
