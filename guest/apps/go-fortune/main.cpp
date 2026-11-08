// SPDX-License-Identifier: Apache-2.0
//
// Fortune Cards: a 22-card deck of our own. Draw past, present and next, then tap a card to read it.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct Card { const char* name; const char* line; uint8_t shape; };
const Card kDeck[] = {{"The Lantern","A small light is enough",0},{"The River","Let it carry you",1},{"The Key","An answer unlocks soon",2},{"The Owl","Watch before you speak",3},{"The Bridge","Cross it, do not study it",4},{"The Kite","Ease off and rise",5},{"The Anvil","Pressure makes the shape",6},{"The Garden","Patience grows it",7},{"The Mirror","It is about you",0},{"The Compass","You knew the way",1},{"The Storm","Loud, and soon over",2},{"The Seed","Begin very small",3},{"The Tower Clock","It is time now",4},{"The Fox","Be clever, stay kind",5},{"The Well","Ask for help",6},{"The Moth","Drawn to the wrong flame",7},{"The Lighthouse","Someone is watching for you",0},{"The Boat","Leave the harbour",1},{"The Candle","Do less, burn longer",2},{"The Crow","Bad news travels, ignore it",3},{"The Hearth","Go home for a while",4},{"The Comet","A rare chance passes",5}};
constexpr int32_t kN = sizeof(kDeck) / sizeof(kDeck[0]);
const char* const kPos[] = {"Was", "Now", "Next"};
constexpr Color kInk[] = {Rgb(255,210,110),Rgb(120,200,255),Rgb(255,130,150),Rgb(170,240,150),Rgb(200,150,255),Rgb(255,170,90),Rgb(150,230,220),Rgb(240,240,180)};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(26, 14, 44)) { Deal(); }
    void Tick(uint32_t) { ++f_; Draw(); }
    void Tap(int32_t x, int32_t y) { if (open_ >= 0) { open_ = -1; return; } if (y > 208) { Deal(); return; } const int32_t i = m::Clamp((x - 10) / 94, 0, 2); if (!up_[i]) { up_[i] = true; Beep(a_, 500U + static_cast<uint32_t>(i) * 120U, 90U, 180U, W::kTriangle); } else { open_ = i; } }
    void Button(KeyCode k) { if (k == kBoot) { Deal(); } }

   private:
    void Deal() { for (int32_t i = 0; i < 3; ++i) { bool dup; do { c_i_[i] = static_cast<int32_t>(a_.random().Below(kN)); dup = false; for (int32_t j = 0; j < i; ++j) { dup = dup || c_i_[j] == c_i_[i]; } } while (dup); up_[i] = false; } open_ = -1; }
    void Sym(int32_t cx, int32_t cy, const Card& k, int32_t s) {
        const Color col = kInk[k.shape];
        switch (k.shape) {
            case 0: c_.Disc(cx, cy, s, col); c_.Disc(cx, cy, s / 2, Rgb(30, 20, 60)); break;
            case 1: for (int32_t x = -s; x <= s; ++x) { c_.Fill(cx + x, cy + static_cast<int32_t>(m::Sin(static_cast<float>(x) * 0.25F) * 6.0F), 2, 4, col); c_.Fill(cx + x, cy + 12 + static_cast<int32_t>(m::Sin(static_cast<float>(x) * 0.25F) * 6.0F), 2, 4, col); } break;
            case 2: c_.Line(cx - s, cy - s, cx + s, cy + s, col, 3); c_.Line(cx + s, cy - s, cx - s, cy + s, col, 3); break;
            case 3: c_.Ring(cx, cy, s, col); c_.Ring(cx, cy, s - 1, col); c_.Disc(cx, cy, 4, col); break;
            case 4: c_.Fill(cx - s, cy - 3, 2 * s, 6, col); c_.Fill(cx - s, cy - s, 6, 2 * s, col); c_.Fill(cx + s - 6, cy - s, 6, 2 * s, col); break;
            case 5: for (int32_t i = 0; i <= s; ++i) { c_.Fill(cx - i, cy - s + i, 2 * i + 1, 1, col); c_.Fill(cx - i, cy + s - i, 2 * i + 1, 1, col); } break;
            case 6: c_.Fill(cx - s, cy + 2, 2 * s, s / 2, col); c_.Fill(cx - s / 2, cy - s, s, s + 2, col); break;
            default: for (int32_t i = 0; i < 8; ++i) { const float t = static_cast<float>(i) * m::kTwoPi / 8.0F; c_.Line(cx, cy, cx + static_cast<int32_t>(m::Cos(t) * static_cast<float>(s)), cy + static_cast<int32_t>(m::Sin(t) * static_cast<float>(s)), col, 2); } break;
        }
    }
    void Draw() {
        c_.Begin();
        for (int32_t i = 0; i < 3; ++i) {
            const int32_t x = 12 + i * 94, y = 30; c_.Text(x + 42, 8, kPos[i], kGrey, kSmall, true);
            if (up_[i]) { const Card& k = kDeck[c_i_[i]]; c_.Round(x, y, 84, 150, 8, Rgb(244, 234, 210)); c_.Fill(x + 6, y + 6, 72, 138, Rgb(36, 24, 70)); Sym(x + 42, y + 62, k, 24); c_.Text(x + 42, y + 112, k.name + 4, kWhite, kSmall, true); }
            else { c_.Round(x, y, 84, 150, 8, Rgb(90, 50, 150)); for (int32_t a = 0; a < 5; ++a) { c_.Frame(x + 8 + a * 7, y + 8 + a * 12, 68 - a * 14, 134 - a * 24, Rgb(190, 150, 240)); } }
        }
        c_.Text(kW / 2, 196, "Tap to turn, tap again to read", kGrey, kSmall, true); c_.Text(kW / 2, 216, "New reading", Rgb(255, 210, 110), kSmall, true);
        if (open_ >= 0) { const Card& k = kDeck[c_i_[open_]]; Banner(c_, k.name, k.line, kInk[k.shape]); }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t c_i_[3]{}, f_{}, open_{-1}; bool up_[3]{};
};
}  // namespace
PORTKIT_MAIN(Game)
