// SPDX-License-Identifier: Apache-2.0
//
// Card Coach: flash cards for capitals, times tables and Hindi numbers. Cards you miss come back sooner.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct C { const char* q; const char* a; };
const C kCap[] = {{"India","New Delhi"},{"Japan","Tokyo"},{"France","Paris"},{"Egypt","Cairo"},{"Brazil","Brasilia"},{"Canada","Ottawa"},{"Kenya","Nairobi"},{"Spain","Madrid"},{"Thailand","Bangkok"},{"Nepal","Kathmandu"},{"Peru","Lima"},{"Norway","Oslo"},{"Turkey","Ankara"},{"Australia","Canberra"},{"Vietnam","Hanoi"},{"Germany","Berlin"}};
const C kHin[] = {{"1","ek"},{"2","do"},{"3","teen"},{"4","chaar"},{"5","paanch"},{"6","chhah"},{"7","saat"},{"8","aath"},{"9","nau"},{"10","das"},{"20","bees"},{"50","pachaas"},{"100","sau"}};
constexpr int32_t kNCap = sizeof(kCap) / sizeof(kCap[0]), kNHin = sizeof(kHin) / sizeof(kHin[0]);
const char* const kDeck[] = {"Capitals","Times tables","Hindi numbers"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(24, 34, 60)), p_(a) {}
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t x, int32_t y) { if (deck_ < 0) { if (y > 56) { Open(m::Clamp((y - 56) / 60, 0, 2)); } return; } if (y < 34) { deck_ = -1; return; } if (!flip_) { flip_ = true; Beep(a_, 600U, 30U); return; } Grade(x > kW / 2); }
    void Button(KeyCode k) { if (k == kBoot) { deck_ = -1; } }

   private:
    int32_t Count() const { return deck_ == 0 ? kNCap : (deck_ == 1 ? 40 : kNHin); }
    void Open(int32_t d) { deck_ = d; for (int32_t i = 0; i < Count(); ++i) { box_[i] = 0U; } seen_ = right_ = 0; Next(); }
    void Next() { flip_ = false; int32_t best = -1; uint32_t top = 0U; for (int32_t i = 0; i < Count(); ++i) { const uint32_t w = (4U - m::Min<uint32_t>(box_[i], 3U)) * (4U - m::Min<uint32_t>(box_[i], 3U)) * (a_.random().Below(30U) + 1U); if (i != cur_ && w > top) { top = w; best = i; } } cur_ = best < 0 ? 0 : best; }
    void Grade(bool knew) { ++seen_; if (knew) { ++right_; if (box_[cur_] < 3U) { ++box_[cur_]; } Beep(a_, 880U, 60U); } else { box_[cur_] = 0U; Beep(a_, 220U, 80U); } Next(); }
    void Text(char* q, char* a) { if (deck_ == 0) { Copy(q, kCap[cur_].q); Copy(a, kCap[cur_].a); } else if (deck_ == 2) { Copy(q, kHin[cur_].q); Copy(a, kHin[cur_].a); } else { const int32_t x = 2 + cur_ / 5 * 1 + cur_ % 4, y = 2 + cur_ % 9; char t[8]; Canvas::Format(t, x); int32_t n = Copy(q, t); q[n++] = ' '; q[n++] = 'x'; q[n++] = ' '; Canvas::Format(q + n, y); Canvas::Format(a, x * y); } }
    static int32_t Copy(char* out, const char* s) { int32_t n = 0; while (s[n] != '\0') { out[n] = s[n]; ++n; } out[n] = '\0'; return n; }
    void Draw() {
        c_.Begin();
        if (deck_ < 0) { c_.Text(kW / 2, 14, "CARD COACH", Rgb(255, 214, 110), kLarge, true); for (int32_t i = 0; i < 3; ++i) { c_.Round(30, 58 + i * 60, kW - 60, 50, 10, Rgb(50, 70, 120)); c_.Text(kW / 2, 72 + i * 60, kDeck[i], kWhite, kMedium, true); } }
        else { char q[20], a[20]; Text(q, a); c_.Text(10, 8, kDeck[deck_], kGrey); c_.Number(kW - 40, 8, right_, Rgb(120, 230, 150)); c_.Text(kW - 28, 8, "/", kGrey); c_.Number(kW - 16, 8, seen_, kWhite, kSmall, true); c_.Round(30, 36, kW - 60, 130, 12, flip_ ? Rgb(255, 244, 214) : Rgb(70, 100, 170)); c_.Text(kW / 2, 76, flip_ ? a : q, flip_ ? Rgb(40, 40, 60) : kWhite, kLarge, true); if (flip_) { c_.Text(kW / 2, 48, q, Rgb(120, 110, 90), kSmall, true); c_.Round(10, 178, 132, 52, 10, Rgb(190, 80, 80)); c_.Text(76, 194, "Again", kWhite, kSmall, true); c_.Round(154, 178, 132, 52, 10, Rgb(70, 160, 110)); c_.Text(220, 194, "Knew it", kWhite, kSmall, true); } else { c_.Text(kW / 2, 196, "Tap to turn over", kGrey, kSmall, true); } }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; uint8_t box_[64]{}; int32_t deck_{-1}, cur_{}, seen_{}, right_{}; bool flip_{};
};
}  // namespace
PORTKIT_MAIN(Game)
