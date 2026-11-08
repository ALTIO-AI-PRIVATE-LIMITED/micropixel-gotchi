// SPDX-License-Identifier: Apache-2.0
//
// Maker Calc: resistor colour bands, Ohm's law and LED resistor sums.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kBand[10] = {"Black","Brown","Red","Orange","Yellow","Green","Blue","Violet","Grey","White"};
constexpr Color kK[10] = {Rgb(30,30,34),Rgb(120,70,30),Rgb(220,40,40),Rgb(250,130,30),Rgb(245,220,60),Rgb(60,170,80),Rgb(50,90,220),Rgb(150,70,200),Rgb(140,140,150),kWhite};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(20, 28, 34)) {}
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { if (y < 38) { tab_ = m::Clamp(x / 98, 0, 2); return; } if (tab_ == 0) { if (y > 170) { b_[m::Clamp((x - 30) / 80, 0, 2)] = (b_[m::Clamp((x - 30) / 80, 0, 2)] + 1) % 10; Beep(a_, 700U, 12U, 70U); } } else { const int32_t row = m::Clamp((y - 56) / 62, 0, 2), d = x > kW / 2 ? 1 : -1; Change(row, d); } }

   private:
    void Change(int32_t r, int32_t d) { float& v = tab_ == 1 ? (r == 0 ? volt_ : amp_) : (r == 0 ? sup_ : (r == 1 ? vf_ : ma_)); if (tab_ == 1 && r == 2) { return; } const float step = tab_ == 2 && r == 2 ? 1.0F : (tab_ == 1 && r == 1 ? 0.01F : 0.1F); v = m::Clamp(v + static_cast<float>(d) * step, 0.0F, 100.0F); }
    static void Fix(char* s, float v, int32_t dec) { auto n = static_cast<int32_t>(v); Canvas::Format(s, n); if (dec == 0) { return; } int32_t l = 0; while (s[l] != '\0') { ++l; } s[l++] = '.'; float f = v - static_cast<float>(n); for (int32_t i = 0; i < dec; ++i) { f *= 10.0F; const auto d = static_cast<int32_t>(f); s[l++] = static_cast<char>('0' + m::Clamp(d, 0, 9)); f -= static_cast<float>(d); } s[l] = '\0'; }
    void Ohm(char* s) const { float r = 1.0F; for (int32_t i = 0; i < b_[2]; ++i) { r *= 10.0F; } r *= static_cast<float>(b_[0] * 10 + b_[1]); Fix(s, r >= 1e6F ? r / 1e6F : (r >= 1e3F ? r / 1e3F : r), r >= 1e3F ? 1 : 0); int32_t l = 0; while (s[l] != '\0') { ++l; } if (r >= 1e6F) { s[l++] = ' '; s[l++] = 'M'; } else if (r >= 1e3F) { s[l++] = ' '; s[l++] = 'k'; } s[l++] = ' '; s[l++] = 'o'; s[l++] = 'h'; s[l++] = 'm'; s[l++] = 's'; s[l] = '\0'; }
    void Row(int32_t r, const char* name, float v, int32_t dec, const char* unit) { const int32_t y = 56 + r * 62; c_.Text(kW / 2, y, name, kGrey, kSmall, true); c_.Round(10, y + 18, 56, 36, 8, Rgb(50, 62, 80)); c_.Text(38, y + 26, "-", kWhite, kMedium, true); c_.Round(230, y + 18, 56, 36, 8, Rgb(50, 62, 80)); c_.Text(258, y + 26, "+", kWhite, kMedium, true); char s[20]; Fix(s, v, dec); int32_t l = 0; while (s[l] != '\0') { ++l; } s[l++] = ' '; for (int32_t i = 0; unit[i] != '\0'; ++i) { s[l++] = unit[i]; } s[l] = '\0'; c_.Text(kW / 2, y + 24, s, kWhite, kMedium, true); }
    void Draw() {
        c_.Begin(); const char* const t[] = {"Resistor","Ohm's law","LED"}; for (int32_t i = 0; i < 3; ++i) { c_.Round(3 + i * 98, 4, 94, 30, 6, i == tab_ ? Rgb(90, 170, 255) : Rgb(44, 54, 70)); c_.Text(50 + i * 98, 10, t[i], kWhite, kSmall, true); }
        if (tab_ == 0) { c_.Fill(0, 96, kW, 8, Rgb(170, 170, 170)); c_.Round(54, 58, 188, 84, 26, Rgb(214, 186, 140)); for (int32_t i = 0; i < 3; ++i) { c_.Fill(86 + i * 42, 58, 22, 84, kK[b_[i]]); } c_.Fill(214, 58, 12, 84, Rgb(200, 170, 70)); char s[24]; Ohm(s); c_.Text(kW / 2, 148, s, kWhite, kMedium, true); for (int32_t i = 0; i < 3; ++i) { c_.Round(30 + i * 80, 178, 76, 54, 8, kK[b_[i]]); c_.Text(68 + i * 80, 196, kBand[b_[i]], b_[i] == 9 || b_[i] == 4 ? Rgb(20, 20, 30) : kWhite, kSmall, true); } }
        else if (tab_ == 1) { Row(0, "Voltage", volt_, 1, "V"); Row(1, "Current", amp_, 2, "A"); const float r = amp_ > 0.0F ? volt_ / amp_ : 0.0F; Row(2, "Resistance", r, 1, "ohm"); }
        else { Row(0, "Supply", sup_, 1, "V"); Row(1, "LED drop", vf_, 1, "V"); Row(2, "Current", ma_, 0, "mA"); const float r = ma_ > 0.0F ? (sup_ - vf_) / (ma_ / 1000.0F) : 0.0F; char s[20]; Fix(s, m::Max(r, 0.0F), 0); int32_t l = 0; while (s[l] != '\0') { ++l; } s[l++] = ' '; s[l++] = 'o'; s[l++] = 'h'; s[l++] = 'm'; s[l] = '\0'; c_.Text(kW / 2, 38, s, Rgb(255, 214, 110), kMedium, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t tab_{}, b_[3]{1, 0, 2}; float volt_{5.0F}, amp_{0.1F}, sup_{5.0F}, vf_{2.0F}, ma_{10.0F};
};
}  // namespace
PORTKIT_MAIN(Game)
