// SPDX-License-Identifier: Apache-2.0
//
// Quick Notes: type up to eight short notes. They stay on the Gotchi.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kMax = 8, kLen = 34; 
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(246, 238, 200), 500U, 44U), p_(a) { (void)p_.Load("notes", reinterpret_cast<uint8_t*>(n_), sizeof(n_)); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) {
        if (edit_ >= 0) { if (y >= Keyboard::kTop) { Key(Keyboard::Press(x, y)); } return; }
        if (y > 208) { if (x < 148) { New(); } else { Delete(); } return; }
        const int32_t i = m::Clamp((y - 4) / 25, 0, kMax - 1); if (n_[i][0] != '\0') { if (sel_ == i) { edit_ = i; } else { sel_ = i; } }
    }
    void Button(KeyCode k) { if (k == kBoot && edit_ >= 0) { Close(); } }

   private:
    void New() { for (int32_t i = 0; i < kMax; ++i) { if (n_[i][0] == '\0') { edit_ = i; sel_ = i; return; } } Beep(a_, 180U, 80U); }
    void Delete() { if (sel_ < 0 || n_[sel_][0] == '\0') { return; } for (int32_t i = sel_; i + 1 < kMax; ++i) { for (int32_t c = 0; c < kLen; ++c) { n_[i][c] = n_[i + 1][c]; } } n_[kMax - 1][0] = '\0'; sel_ = -1; Save(); }
    void Save() { p_.Save("notes", reinterpret_cast<const uint8_t*>(n_), sizeof(n_)); }
    void Close() { edit_ = -1; Save(); }
    void Key(char k) { if (k == 0) { return; } char* s = n_[edit_]; int32_t l = 0; while (s[l] != '\0') { ++l; } if (k == '\n') { Close(); return; } if (k == '\b') { if (l > 0) { s[l - 1] = '\0'; } return; } if (l < kLen - 1) { s[l] = k; s[l + 1] = '\0'; Beep(a_, 760U, 8U, 60U); } }
    void Draw() {
        c_.Begin();
        if (edit_ >= 0) { c_.Fill(6, 8, kW - 12, 110, kWhite); char line[24]; const char* s = n_[edit_]; int32_t i = 0, r = 0; while (s[i] != '\0') { int32_t n = 0; while (n < 22 && s[i] != '\0') { line[n++] = s[i++]; } line[n] = '\0'; c_.Text(14, 16 + r * 24, line, Rgb(30, 30, 50), kMedium); ++r; } Keyboard::Draw(c_, Rgb(90, 90, 120), kWhite); }
        else { bool any = false; for (int32_t i = 0; i < kMax; ++i) { const int32_t y = 4 + i * 25; if (n_[i][0] == '\0') { continue; } any = true; c_.Fill(6, y, kW - 12, 23, i == sel_ ? Rgb(255, 214, 110) : kWhite); c_.Text(12, y + 3, n_[i], Rgb(30, 30, 50)); } if (!any) { c_.Text(kW / 2, 90, "No notes yet", Rgb(120, 110, 80), kMedium, true); } c_.Round(6, 212, 138, 26, 6, Rgb(70, 130, 90)); c_.Text(75, 216, "New note", kWhite, kSmall, true); c_.Round(152, 212, 138, 26, 6, Rgb(180, 80, 80)); c_.Text(221, 216, "Delete", kWhite, kSmall, true); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; char n_[kMax][kLen]{}; int32_t edit_{-1}, sel_{-1};
};
}  // namespace
PORTKIT_MAIN(Game)
