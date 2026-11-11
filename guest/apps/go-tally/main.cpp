// SPDX-License-Identifier: Apache-2.0
//
// Tally: four big counters for scores, stock or anything else. Saved on the Gotchi.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr Color kK[4] = {Rgb(230,80,90),Rgb(70,150,240),Rgb(90,190,120),Rgb(240,170,60)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 16, 24)), p_(a) { for (int32_t i = 0; i < 4; ++i) { n_[i] = static_cast<int32_t>(p_.Get(Key(i))); } }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { const int32_t i = m::Clamp(x / 148, 0, 1) + m::Clamp(y / 120, 0, 1) * 2; const int32_t rx = x - (i % 2) * 148, ry = y - (i / 2) * 120; n_[i] += rx < 50 && ry > 70 ? -1 : 1; if (rx < 50 && ry > 70 && n_[i] < 0) { n_[i] = 0; } p_.Set(Key(i), static_cast<uint32_t>(n_[i])); Beep(a_, 500U + static_cast<uint32_t>(i) * 120U, 25U, 110U); }
    void Button(KeyCode k) { if (k == kBoot) { for (int32_t i = 0; i < 4; ++i) { n_[i] = 0; p_.Set(Key(i), 0U); } } }

   private:
    static const char* Key(int32_t i) { static const char* const k[4] = {"a", "b", "c", "d"}; return k[i]; }
    void Draw() { c_.Begin(); for (int32_t i = 0; i < 4; ++i) { const int32_t x = 3 + (i % 2) * 148, y = 3 + (i / 2) * 120; c_.Round(x, y, 142, 114, 12, kK[i]); c_.Number(x + 71, y + 34, n_[i], kWhite, kTitle, true); c_.Round(x + 6, y + 76, 40, 32, 8, kK[i].Darkened(80U)); c_.Text(x + 26, y + 82, "-", kWhite, kMedium, true); c_.Text(x + 96, y + 84, "tap +1", kWhite, kSmall, true); } c_.Present(); }
    Application& a_; Canvas c_; Prefs p_; int32_t n_[4]{};
};
}  // namespace
PORTKIT_MAIN(Game)
