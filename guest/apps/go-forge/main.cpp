// SPDX-License-Identifier: Apache-2.0
//
// Coin Forge: tap to mint coins and buy machines that mint for you. Progress is saved.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kName[] = {"Helper","Hammer","Furnace","Factory"}; constexpr uint32_t kBase[] = {15U,100U,600U,4000U}, kRate[] = {1U,6U,30U,160U};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(38, 24, 14)), p_(a) { coins_ = p_.Get("coins"); for (int32_t i = 0; i < 4; ++i) { const char k[3] = {'m', static_cast<char>('0' + i), '\0'}; n_[i] = p_.Get(k); } }
    void Tick(uint32_t ms) { acc_ += ms; if (acc_ >= 1000U) { acc_ -= 1000U; coins_ += Rate(); if (++t_ % 8U == 0U) { Save(); } } if (bump_ > 0) { --bump_; } Draw(); }
    void Press(int32_t x, int32_t y) { if (y < 120) { coins_ += 1U + Rate() / 20U; bump_ = 4; Beep(a_, 1200U, 18U, 100U); (void)x; } else { Buy(m::Clamp(x / 74, 0, 3)); } }
    void Button(KeyCode k) { if (k == kBoot) { coins_ += 1U + Rate() / 20U; bump_ = 4; } }

   private:
    uint32_t Rate() const { uint32_t r = 0U; for (int32_t i = 0; i < 4; ++i) { r += kRate[i] * n_[i]; } return r; }
    uint32_t Cost(int32_t i) const { uint32_t c = kBase[i]; for (uint32_t k = 0U; k < n_[i] && k < 40U; ++k) { c = c + c / 7U + 1U; } return c; }
    void Buy(int32_t i) { const uint32_t c = Cost(i); if (coins_ >= c) { coins_ -= c; ++n_[i]; Beep(a_, 660U, 60U); Save(); } else { Beep(a_, 180U, 40U); } }
    void Save() { p_.Set("coins", coins_); for (int32_t i = 0; i < 4; ++i) { const char k[3] = {'m', static_cast<char>('0' + i), '\0'}; p_.Set(k, n_[i]); } }
    void Draw() {
        c_.Begin();
        const int32_t r = 34 + bump_ * 2; c_.Disc(148, 74, r, Rgb(240, 180, 60)); c_.Disc(148, 74, r - 6, Rgb(255, 214, 110)); c_.Text(148, 62, "G", Rgb(150, 100, 30), kLarge, true);
        c_.Number(10, 8, static_cast<int32_t>(coins_), Rgb(255, 224, 130), kLarge); char s[16]; Canvas::Format(s, static_cast<int32_t>(Rate())); int32_t q = 0; while (s[q] != '\0') { ++q; } s[q++] = '/'; s[q++] = 's'; s[q] = '\0'; c_.Text(kW - 10 - 8 * q, 14, s, kGrey);
        for (int32_t i = 0; i < 4; ++i) { const int32_t x = 3 + i * 74; const bool can = coins_ >= Cost(i); c_.Round(x, 126, 68, 110, 8, can ? Rgb(120, 76, 30) : Rgb(62, 44, 28)); c_.Text(x + 34, 134, kName[i], kWhite, kSmall, true); c_.Number(x + 34, 160, static_cast<int32_t>(n_[i]), kWhite, kLarge, true); c_.Number(x + 34, 206, static_cast<int32_t>(Cost(i)), can ? Rgb(255, 224, 130) : kGrey, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; uint32_t coins_{}, n_[4]{}, acc_{}, t_{}; int32_t bump_{};
};
}  // namespace
PORTKIT_MAIN(Game)
