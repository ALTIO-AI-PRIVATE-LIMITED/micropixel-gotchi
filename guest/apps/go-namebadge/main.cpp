// SPDX-License-Identifier: Apache-2.0
//
// Name Tag: your name in big letters for meetups, with four looks.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kLen = 22; constexpr Color kBg[] = {Rgb(255,90,80),Rgb(30,60,150),Rgb(20,20,28),Rgb(250,214,90)}; constexpr Color kInk[] = {kWhite,kWhite,Rgb(120,255,190),Rgb(30,30,50)};
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(20, 20, 30), 500U, 44U), p_(a) { (void)p_.Load("name", reinterpret_cast<uint8_t*>(n_), kLen); (void)p_.Load("line", reinterpret_cast<uint8_t*>(l_), kLen); look_ = static_cast<int32_t>(p_.Get("look")) % 4; if (n_[0] == '\0') { edit_ = 0; } }
    void Tick(uint32_t) { ++f_; Draw(); }
    void Press(int32_t x, int32_t y) { if (edit_ >= 0) { if (y >= Keyboard::kTop) { Key(Keyboard::Press(x, y)); } return; } if (y < 40 && x > 220) { edit_ = 0; return; } look_ = (look_ + 1) % 4; p_.Set("look", static_cast<uint32_t>(look_)); }

   private:
    void Key(char k) { if (k == 0) { return; } char* s = edit_ == 0 ? n_ : l_; int32_t l = 0; while (s[l] != '\0') { ++l; } if (k == '\n') { if (edit_ == 0) { edit_ = 1; } else { edit_ = -1; p_.Save("name", reinterpret_cast<const uint8_t*>(n_), kLen); p_.Save("line", reinterpret_cast<const uint8_t*>(l_), kLen); } return; } if (k == '\b') { if (l > 0) { s[l - 1] = '\0'; } return; } if (l < kLen - 1) { s[l] = k; s[l + 1] = '\0'; } }
    void Draw() {
        c_.Begin();
        if (edit_ >= 0) { c_.Text(14, 12, edit_ == 0 ? "Your name" : "A line about you", kGrey, kMedium); c_.Fill(6, 44, kW - 12, 44, kWhite); c_.Text(14, 54, edit_ == 0 ? n_ : l_, Rgb(30, 30, 50), kMedium); Keyboard::Draw(c_, Rgb(60, 66, 90), kWhite); c_.Present(); return; }
        c_.Fill(0, 0, kW, kH, kBg[look_]); const Color k = kInk[look_];
        c_.Fill(0, 0, kW, 40, k, 40U); c_.Text(14, 10, "HELLO I AM", k, kMedium); c_.Text(260, 10, "Edit", k, kSmall, true);
        int32_t len = 0; while (n_[len] != '\0') { ++len; } c_.Text(kW / 2, 92, n_, k, len > 12 ? kMedium : (len > 8 ? kLarge : kTitle), true); c_.Text(kW / 2, 164, l_, k, kSmall, true);
        if (look_ == 2) { for (int32_t i = 0; i < 18; ++i) { c_.Fill((i * 41 + f_) % kW, 200 + i % 3 * 12, 10, 2, k); } } else { c_.Fill(40, 150, kW - 80, 3, k); }
        c_.Present();
    }
    Canvas c_; Prefs p_; char n_[kLen]{}, l_[kLen]{}; int32_t edit_{-1}, look_{}, f_{};
};
}  // namespace
PORTKIT_MAIN(Game)
