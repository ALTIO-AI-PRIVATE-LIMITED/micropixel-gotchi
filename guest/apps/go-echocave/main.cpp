// SPDX-License-Identifier: Apache-2.0
//
// Echo Cave: a short text adventure with choices, fights, gold and a lantern.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
struct Node { const char* text; const char* a; int8_t ta; const char* b; int8_t tb; int8_t fight; int8_t gold; int8_t lamp; };
const Node kN[] = {
/*0*/{"You wake at the mouth of a cave. Wind hums inside.","Go in",1,"Search the camp",2,0,0,0},
/*1*/{"It is black ahead. Water drips on your neck.","Feel along the wall",3,"Go back out",0,0,0,0},
/*2*/{"Under a tarp you find a lantern and a coin purse.","Take it, go in",1,"Take it, go in",1,0,5,1},
/*3*/{"A fork. Left smells of smoke, right of the sea.","Left",4,"Right",6,0,0,0},
/*4*/{"A bat swarm drops from the roof.","Press on",5,"Run to the fork",3,1,0,0},
/*5*/{"An old forge. A coal still glows beside a chest.","Open the chest",9,"Warm your hands",7,0,0,0},
/*6*/{"A tide pool glitters. Something moves under it.","Reach in",8,"Walk past",10,0,0,0},
/*7*/{"Warmth returns. You feel stronger.","Open the chest",9,"Open the chest",9,-2,0,0},
/*8*/{"A crab snaps at you. Then you see a pearl.","Keep it",10,"Keep it",10,1,9,0},
/*9*/{"Gold! And a note: the deep door needs a light.","Go deeper",10,"Go deeper",10,0,12,0},
/*10*/{"A stone door with a socket shaped like a lantern.","Use the lantern",11,"Force it",12,0,0,0},
/*11*/{"The door swings open and the echo sings your name.","Step through",13,"Step through",13,0,0,0},
/*12*/{"The door wakes. A stone guard steps out.","Fight on",13,"Fight on",13,2,0,0},
/*13*/{"Daylight. A valley, and you are rich in stories.","Play again",0,"Play again",0,0,0,0}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(16, 14, 26)) { Enter(0); }
    void Tick(uint32_t) { Draw(); }
    void Press(int32_t, int32_t y) { if (dead_) { Reset(); return; } if (y > 116) { Choose(y < 176 ? 0 : 1); } }
    void Button(KeyCode k) { if (dead_) { Reset(); return; } if (k == kVolDown) { Choose(0); } else if (k == kVolUp) { Choose(1); } }

   private:
    void Reset() { hp_ = 20; gold_ = 0; lamp_ = false; dead_ = false; Enter(0); }
    void Enter(int32_t i) {
        cur_ = i; const Node& n = kN[i]; note_ = ""; if (i == 0) { hp_ = 20; gold_ = 0; lamp_ = false; }
        gold_ += n.gold; if (n.lamp != 0) { lamp_ = true; }
        if (n.fight < 0) { hp_ = m::Min(hp_ + 8, 30); }
        if (n.fight > 0) { const int32_t lost = (static_cast<int32_t>(a_.random().Below(5U)) + 2) * n.fight; hp_ -= lost; note_ = "You fought it off and got hurt."; Beep(a_, 140U, 120U, 240U, W::kSquare); if (hp_ <= 0) { dead_ = true; note_ = "The cave keeps you."; } }
    }
    void Choose(int32_t c) { const Node& n = kN[cur_]; int32_t t = c == 0 ? n.ta : n.tb; if (cur_ == 10 && c == 0 && !lamp_) { note_ = "You have no lantern."; return; } Beep(a_, 500U, 30U); Enter(t); }
    void Wrap(const char* s, int32_t y, int32_t width) { char line[44]; int32_t n = 0, last = -1, row = 0, i = 0; for (;;) { const char ch = s[i]; if (ch == ' ') { last = n; } if (ch == '\0' || n >= width) { const int32_t end = ch == '\0' ? n : (last > 0 ? last : n); line[end] = '\0'; c_.Text(14, y + row * 20, line, kWhite); if (ch == '\0') { return; } i -= n - end; if (s[i] == ' ') { ++i; } n = 0; last = -1; ++row; continue; } line[n++] = ch; ++i; } }
    void Draw() {
        c_.Begin();
        const Node& n = kN[cur_];
        c_.Fill(0, 0, kW, 26, Rgb(36, 30, 54)); c_.Text(10, 5, "HP", Rgb(255, 120, 120)); c_.Number(34, 5, hp_, kWhite); c_.Text(90, 5, "Gold", Rgb(255, 214, 110)); c_.Number(130, 5, gold_, kWhite); c_.Text(200, 5, lamp_ ? "Lantern" : "No light", lamp_ ? Rgb(255, 214, 110) : kGrey);
        Wrap(dead_ ? "It ends here, for now." : n.text, 36, 36); c_.Text(14, 96, note_, Rgb(255, 170, 120));
        if (dead_) { c_.Round(14, 122, 268, 50, 10, Rgb(120, 40, 60)); c_.Text(148, 136, "Start again", kWhite, kSmall, true); }
        else { c_.Round(14, 122, 268, 50, 10, Rgb(50, 60, 110)); c_.Text(148, 136, n.a, kWhite, kSmall, true); if (n.tb != n.ta || n.b != n.a) { c_.Round(14, 180, 268, 50, 10, Rgb(50, 60, 110)); c_.Text(148, 194, n.b, kWhite, kSmall, true); } }
        c_.Present();
    }
    Application& a_; Canvas c_; int32_t cur_{}, hp_{20}, gold_{}; bool lamp_{}, dead_{}; const char* note_{""};
};
}  // namespace
PORTKIT_MAIN(Game)
