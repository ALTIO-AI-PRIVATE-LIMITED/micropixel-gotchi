// SPDX-License-Identifier: Apache-2.0
//
// Morse Trainer: see a letter, key it with short and long presses, and learn the code.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kCode[26] = {".-","-...","-.-.","-..",".","..-.","--.","....","..",".---","-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-","..-","...-",".--","-..-","-.--","--.."};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 24, 22)), ck_(a), p_(a) { best_ = p_.Get("best"); New(); }
    void Tick(uint32_t ms) { idle_ += ms; if (n_ > 0 && !down_ && idle_ > 900U) { Check(); } if (fb_ > 0) { --fb_; } ++f_; Draw(); }
    void Press(int32_t, int32_t) { Down(); }
    void Tap(int32_t, int32_t) { Up(); }
    void Key(const micropixel::KeyEvent& e) { if (e.code() != kBoot) { if (e.phase() == micropixel::KeyPhase::kDown) { Hint(); } return; } if (e.phase() == micropixel::KeyPhase::kDown) { Down(); } else { Up(); } }

   private:
    void New() { letter_ = static_cast<int32_t>(a_.random().Below(26U)); n_ = 0; hint_ = false; }
    void Hint() { hint_ = true; streak_ = 0; const char* c = kCode[letter_]; Beep(a_, 640U, c[0] == '.' ? 90U : 260U, 200U); }
    void Down() { down_ = true; t0_ = ck_.Now(); Beep(a_, 640U, 180U, 170U); }
    void Up() { if (!down_) { return; } down_ = false; if (n_ < 6) { in_[n_++] = ck_.Now() - t0_ < 240U ? '.' : '-'; } idle_ = 0U; }
    void Check() { const char* c = kCode[letter_]; bool ok = true; int32_t i = 0; for (; i < n_; ++i) { ok = ok && c[i] == in_[i]; } ok = ok && c[i] == '\0'; ok_ = ok; fb_ = 25; if (ok) { ++streak_; if (streak_ > best_) { best_ = streak_; p_.Set("best", best_); } Beep(a_, 880U, 120U); New(); } else { streak_ = 0; n_ = 0; hint_ = true; Beep(a_, 170U, 200U); } }
    void Marks(const char* s, int32_t len, int32_t y, Color k) { int32_t w = 0; for (int32_t i = 0; i < len; ++i) { w += (s[i] == '.' ? 12 : 30) + 8; } int32_t x = (kW - w) / 2; for (int32_t i = 0; i < len; ++i) { if (s[i] == '.') { c_.Disc(x + 6, y + 6, 6, k); x += 20; } else { c_.Round(x, y, 30, 12, 5, k); x += 38; } } }
    void Draw() {
        c_.Begin(); const char s[2] = {static_cast<char>('A' + letter_), '\0'}; int32_t len = 0; while (kCode[letter_][len] != '\0') { ++len; }
        c_.Text(10, 8, "Streak", kGrey); c_.Number(60, 8, static_cast<int32_t>(streak_), kWhite); c_.Text(200, 8, "Best", kGrey); c_.Number(240, 8, static_cast<int32_t>(best_), kWhite);
        c_.Text(kW / 2, 40, s, kWhite, kTitle, true);
        if (hint_) { Marks(kCode[letter_], len, 90, Rgb(255, 214, 110)); } else { c_.Text(kW / 2, 92, "VOL key shows a hint", kDim, kSmall, true); }
        Marks(in_, n_, 136, Rgb(120, 240, 200));
        if (fb_ > 0) { c_.Text(kW / 2, 160, ok_ ? "Right" : "Try again", ok_ ? Rgb(120, 240, 150) : Rgb(255, 130, 110), kMedium, true); }
        c_.Round(20, 192, kW - 40, 42, 10, down_ ? Rgb(120, 240, 200) : Rgb(40, 70, 64)); c_.Text(kW / 2, 203, "Short tap dot, long press dash", kWhite, kSmall, true);
        c_.Present();
    }
    Application& a_; Canvas c_; Millis ck_; Prefs p_; char in_[7]{}; int32_t letter_{}, n_{}, fb_{}, f_{}; uint32_t streak_{}, best_{}, t0_{}, idle_{}; bool down_{}, hint_{}, ok_{};
};
}  // namespace
PORTKIT_MAIN(Game)
