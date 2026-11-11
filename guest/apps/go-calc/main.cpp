// SPDX-License-Identifier: Apache-2.0
//
// Calculator: add, subtract, multiply, divide, percent and sign.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
const char* const kK[20] = {"C","+/-","%","/","7","8","9","x","4","5","6","-","1","2","3","+","0",".","<","="};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 18, 26)) { Clear(); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { if (y < 56) { return; } const int32_t col = m::Clamp(x / 74, 0, 3), row = m::Clamp((y - 56) / 37, 0, 4); Key(kK[row * 4 + col][0], row * 4 + col); Beep(a_, 700U, 12U, 80U); }

   private:
    void Clear() { cur_ = 0.0; acc_ = 0.0; op_ = 0; fresh_ = true; dot_ = 0.0; err_ = false; }
    void Apply() { if (op_ == '+') { acc_ += cur_; } else if (op_ == '-') { acc_ -= cur_; } else if (op_ == 'x') { acc_ *= cur_; } else if (op_ == '/') { if (cur_ == 0.0) { err_ = true; acc_ = 0.0; } else { acc_ /= cur_; } } else { acc_ = cur_; } cur_ = acc_; }
    void Key(char k, int32_t index) {
        if (k >= '0' && k <= '9') { if (fresh_) { cur_ = 0.0; dot_ = 0.0; fresh_ = false; err_ = false; } if (dot_ > 0.0) { cur_ += (k - '0') * dot_; dot_ /= 10.0; } else if (cur_ < 1e10) { cur_ = cur_ * 10.0 + (k - '0'); } return; }
        if (index == 17) { if (fresh_) { cur_ = 0.0; fresh_ = false; } if (dot_ == 0.0) { dot_ = 0.1; } return; }
        if (index == 0) { Clear(); return; } if (index == 1) { cur_ = -cur_; return; } if (index == 2) { cur_ /= 100.0; return; }
        if (index == 18) { cur_ = dot_ == 0.0 ? static_cast<double>(static_cast<int64_t>(cur_ / 10.0)) : cur_; return; }
        if (index == 19) { Apply(); op_ = 0; fresh_ = true; dot_ = 0.0; return; }
        if (!fresh_) { Apply(); } else if (op_ == 0) { acc_ = cur_; } op_ = k; fresh_ = true; dot_ = 0.0;
    }
    void Show(char* s) const { double v = cur_; int32_t n = 0; if (v < 0.0) { s[n++] = '-'; v = -v; } if (v >= 1e12) { const char* big = "Too big"; for (int32_t i = 0; big[i] != '\0'; ++i) { s[i] = big[i]; } s[7] = '\0'; return; } v += 0.0000005; auto whole = static_cast<int64_t>(v); double frac = v - static_cast<double>(whole); char t[20]; int32_t c = 0; do { t[c++] = static_cast<char>('0' + whole % 10); whole /= 10; } while (whole > 0); while (c > 0) { s[n++] = t[--c]; } int32_t dec = 6; char f[8]; int32_t fn = 0; for (int32_t i = 0; i < dec; ++i) { frac *= 10.0; const auto d = static_cast<int32_t>(frac); f[fn++] = static_cast<char>('0' + d); frac -= d; } while (fn > 0 && f[fn - 1] == '0') { --fn; } if (fn > 0) { s[n++] = '.'; for (int32_t i = 0; i < fn; ++i) { s[n++] = f[i]; } } s[n] = '\0'; }
    void Draw() {
        c_.Begin(); char s[32]; if (err_) { s[0] = 'E'; s[1] = 'r'; s[2] = 'r'; s[3] = '\0'; } else { Show(s); } int32_t n = 0; while (s[n] != '\0') { ++n; }
        c_.Text(kW - 8 - n * 15, 14, s, kWhite, kLarge);
        for (int32_t i = 0; i < 20; ++i) { const int32_t x = 3 + (i % 4) * 74, y = 58 + (i / 4) * 37; const bool op = i % 4 == 3 || i == 19; c_.Round(x, y, 70, 34, 7, op ? Rgb(240, 150, 60) : (i < 3 ? Rgb(90, 96, 120) : Rgb(46, 50, 68))); c_.Text(x + 35, y + 7, kK[i], kWhite, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; double cur_{}, acc_{}, dot_{}; char op_{}; bool fresh_{true}, err_{};
};
}  // namespace
PORTKIT_MAIN(Game)
