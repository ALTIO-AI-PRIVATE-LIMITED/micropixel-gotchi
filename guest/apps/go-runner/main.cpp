// SPDX-License-Identifier: Apache-2.0
//
// Rooftop Dash: run across city rooftops. Tap or press BOOT to jump, twice for a double jump.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
struct Roof { float x; int32_t w, top; };
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(26, 22, 58)), p_(a) { best_ = p_.Get("best"); Reset(); }
    void Tick(uint32_t) { if (state_ == 1) { Step(); } ++frame_; Draw(); }
    void Press(int32_t, int32_t) { Act(); }
    void Button(KeyCode k) { if (k == kBoot) { Act(); } }

   private:
    void Reset() {
        float x = 0.0F; int32_t top = 170;
        for (auto& r : roofs_) { r = {x, 120 + static_cast<int32_t>(a_.random().Below(90U)), top}; x += static_cast<float>(r.w) + 44.0F; top = NextTop(top); }
        y_ = 170.0F; vy_ = 0.0F; speed_ = 3.4F; dist_ = 0.0F; jumps_ = 0; dead_ = 0;
    }
    int32_t NextTop(int32_t top) { const int32_t n = top - 34 + static_cast<int32_t>(a_.random().Below(69U)); return n < 110 ? 110 : (n > 200 ? 200 : n); }
    void Act() {
        if (state_ == 0) { state_ = 1; Reset(); return; }
        if (state_ == 2) { if (dead_ > 12) { state_ = 0; } return; }
        if (jumps_ < 2) { vy_ = jumps_ == 0 ? -9.6F : -8.0F; ++jumps_; Beep(a_, jumps_ == 1 ? 520U : 700U, 50U); }
    }
    void Step() {
        speed_ += 0.0016F; dist_ += speed_;
        float last = 0.0F; int32_t lastTop = 170;
        for (auto& r : roofs_) { r.x -= speed_; if (r.x + static_cast<float>(r.w) > last) { last = r.x + static_cast<float>(r.w); lastTop = r.top; } }
        for (auto& r : roofs_) {
            if (r.x + static_cast<float>(r.w) < -4.0F) { r.w = 90 + static_cast<int32_t>(a_.random().Below(120U)); r.top = NextTop(lastTop); r.x = last + 38.0F + speed_ * 6.0F + static_cast<float>(a_.random().Below(30U)); break; }
        }
        const float before = y_;
        vy_ += 0.62F; y_ += vy_;
        for (const auto& r : roofs_) {
            if (64.0F > r.x && 50.0F < r.x + static_cast<float>(r.w)) {
                const float top = static_cast<float>(r.top);
                if (vy_ >= 0.0F && before <= top + 1.0F && y_ >= top) { y_ = top; vy_ = 0.0F; jumps_ = 0; }
            }
        }
        if (y_ > 262.0F) {
            state_ = 2; dead_ = 0; Beep(a_, 130U, 260U, 240U, micropixel::Waveform::kSquare);
            const auto score = static_cast<uint32_t>(dist_ / 12.0F);
            if (score > best_) { best_ = score; p_.Set("best", best_); }
        }
    }
    void Draw() {
        if (state_ == 2) { ++dead_; }
        c_.Begin();
        c_.Disc(232, 48, 22, Rgb(255, 214, 120));
        for (int32_t i = 0; i < 9; ++i) { const int32_t x = ((i * 47) - static_cast<int32_t>(dist_ / 4.0F)) % 340; c_.Fill((x + 340) % 340 - 30, 120 + (i * 37) % 60, 28, 140, Rgb(44, 38, 88)); }
        for (const auto& r : roofs_) {
            const auto x = static_cast<int32_t>(r.x);
            c_.Fill(x, r.top, r.w, kH - r.top, Rgb(58, 62, 110)); c_.Fill(x, r.top, r.w, 5, Rgb(255, 120, 90));
            for (int32_t wx = x + 10; wx < x + r.w - 12; wx += 22) { for (int32_t wy = r.top + 16; wy < kH; wy += 24) { c_.Fill(wx, wy, 10, 12, Rgb(255, 212, 110), (wx + wy) % 3 == 0 ? 220U : 50U); } }
        }
        const auto y = static_cast<int32_t>(y_);
        c_.Round(50, y - 26, 16, 20, 4, Rgb(120, 240, 200)); c_.Fill(60, y - 21, 4, 5, Rgb(20, 24, 40));
        const int32_t leg = jumps_ != 0 ? 0 : (frame_ / 3) % 2 * 5;
        c_.Fill(52 + leg, y - 6, 4, 6, kWhite); c_.Fill(60 - leg, y - 6, 4, 6, kWhite);
        c_.Number(10, 8, static_cast<int32_t>(dist_ / 12.0F), kWhite, kLarge);
        c_.Text(kW - 74, 10, "BEST", kGrey); c_.Number(kW - 36, 10, static_cast<int32_t>(best_), kWhite);
        if (state_ == 0) { Banner(c_, "ROOFTOP DASH", "Tap to run, tap twice to fly", Rgb(255, 150, 90)); }
        if (state_ == 2) { Banner(c_, "OUT OF ROOF", "Tap to go again", Rgb(255, 110, 110)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_;
    Roof roofs_[6]{}; float y_{}, vy_{}, speed_{}, dist_{};
    int32_t jumps_{}, frame_{}, state_{}, dead_{}; uint32_t best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
