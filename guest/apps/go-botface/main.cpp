// SPDX-License-Identifier: Apache-2.0
//
// Bot Buddy: a robot face that looks where you touch, changes mood when tapped and gets dizzy when shaken.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
const char* const kMoods[] = {"happy", "sleepy", "wow", "grumpy", "love"};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(10, 14, 26)), t_(a) {}
    void Tick(uint32_t ms) {
        t_.Poll(); now_ += ms;
        if (t_.jolt()) { dizzy_ = 1800; }
        if (dizzy_ > 0) { dizzy_ -= static_cast<int32_t>(ms); }
        if (--blink_ < -6) { blink_ = 40 + static_cast<int32_t>(a_.random().Below(90U)); }
        if (touch_ > 0) { --touch_; }
        else if (now_ > next_) { next_ = now_ + 1200U + a_.random().Below(2400U); gx_ = static_cast<float>(a_.random().Below(41U)) - 20.0F; gy_ = static_cast<float>(a_.random().Below(21U)) - 10.0F; }
        lx_ += (gx_ + t_.x() * 12.0F - lx_) * 0.2F; ly_ += (gy_ + t_.y() * 8.0F - ly_) * 0.2F;
        Draw();
    }
    void Press(int32_t x, int32_t y) { Look(x, y); moved_ = false; }
    void Drag(int32_t x, int32_t y) { Look(x, y); moved_ = true; }
    void Tap(int32_t, int32_t) { if (!moved_) { Next(); } }
    void Button(KeyCode k) { if (k == kBoot) { Next(); } }

   private:
    void Look(int32_t x, int32_t y) { gx_ = static_cast<float>(x - kW / 2) * 0.17F; gy_ = static_cast<float>(y - kH / 2) * 0.12F; touch_ = 50; }
    void Next() { mood_ = (mood_ + 1) % 5; Beep(a_, 400U + static_cast<uint32_t>(mood_) * 140U, 90U); }
    void Eye(int32_t cx, Color col) {
        const int32_t x = cx + static_cast<int32_t>(lx_), y = 96 + static_cast<int32_t>(ly_);
        if (dizzy_ > 0) { for (int32_t r = 6; r < 32; r += 8) { c_.Ring(x + static_cast<int32_t>(m::Sin(static_cast<float>(now_) * 0.01F + static_cast<float>(r)) * 4.0F), y, r, col); } return; }
        if (mood_ == 4) { c_.Disc(x - 11, y - 8, 13, Rgb(255, 90, 130)); c_.Disc(x + 11, y - 8, 13, Rgb(255, 90, 130)); for (int32_t i = 0; i < 24; ++i) { c_.Fill(x - 24 + i, y + i - 4, 48 - 2 * i, 1, Rgb(255, 90, 130)); } return; }
        int32_t h = mood_ == 1 ? 14 : (mood_ == 2 ? 62 : 48); const int32_t w = mood_ == 2 ? 44 : 40;
        if (blink_ < 0) { h = 6; }
        c_.Round(x - w / 2, y - h / 2, w, h, h > 16 ? 12 : 2, col);
        if (mood_ == 3) { c_.Line(x - 26, y - 34 + (cx < kW / 2 ? 0 : 10), x + 26, y - 34 + (cx < kW / 2 ? 10 : 0), Rgb(10, 14, 26), 8); }
    }
    void Draw() {
        const Color col = mood_ == 3 ? Rgb(255, 120, 80) : (mood_ == 1 ? Rgb(120, 160, 255) : Rgb(90, 240, 220));
        c_.Begin();
        c_.Round(16, 22, kW - 32, 196, 28, Rgb(24, 30, 50));
        c_.Fill(kW / 2 - 3, 2, 6, 20, kDim); c_.Disc(kW / 2, 6, 6, (now_ / 400U) % 2U == 0U ? Rgb(255, 90, 90) : Rgb(120, 40, 50));
        Eye(92, col); Eye(204, col);
        const int32_t y = 176;
        if (dizzy_ > 0) { for (int32_t x = 108; x < 188; x += 2) { c_.Fill(x, y + static_cast<int32_t>(m::Sin(static_cast<float>(x) * 0.25F + static_cast<float>(now_) * 0.01F) * 6.0F), 3, 3, col); } }
        else if (mood_ == 0 || mood_ == 4) { for (int32_t x = -36; x <= 36; ++x) { c_.Fill(148 + x, y + 12 - x * x / 108, 2, 5, col); } }
        else if (mood_ == 1) { c_.Fill(130, y + 2, 36, 5, col); c_.Text(214, 40 + static_cast<int32_t>(now_ / 80U) % 20, "z z", kGrey, kMedium); }
        else if (mood_ == 2) { c_.Ring(148, y, 14, col); c_.Ring(148, y, 13, col); }
        else { for (int32_t x = -30; x <= 30; ++x) { c_.Fill(148 + x, y + x * x / 90, 2, 5, col); } }
        c_.Text(kW / 2, 202, dizzy_ > 0 ? "whoa" : kMoods[mood_], kGrey, kSmall, true);
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_;
    uint32_t now_{}, next_{}; float gx_{}, gy_{}, lx_{}, ly_{};
    int32_t blink_{60}, touch_{}, mood_{}, dizzy_{}; bool moved_{};
};
}  // namespace
PORTKIT_MAIN(Game)
