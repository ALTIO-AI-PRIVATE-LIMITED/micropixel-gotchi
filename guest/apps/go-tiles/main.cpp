// SPDX-License-Identifier: Apache-2.0
//
// Beat Tiles: tap each lane as the tiles reach the line. Three tunes of our own.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kLaneW = 74, kLine = 200;
constexpr uint32_t kHz[] = {262U,294U,330U,392U,440U,523U,587U,659U};
const uint8_t kSong[3][32] = {{0,2,4,2,0,2,4,7,5,4,2,0,1,3,5,3,4,2,0,2,4,5,7,5,4,2,1,0,2,4,2,0},{7,5,4,2,4,5,7,0,7,5,4,5,2,3,4,0,4,5,7,5,4,3,2,1,0,2,4,7,5,4,2,0},{0,0,4,4,5,5,4,3,3,2,2,1,1,0,4,4,3,3,2,2,1,4,4,3,3,2,2,1,0,0,4,0}};
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 12, 34)), p_(a) { best_ = p_.Get("best"); }
    void Tick(uint32_t) { if (state_ == 1) { Step(); } if (fx_ > 0) { --fx_; } Draw(); }
    void Press(int32_t x, int32_t) { if (state_ != 1) { Start(); } else { Hit(m::Clamp(x / kLaneW, 0, 3)); } }
    void Button(KeyCode k) { if (state_ != 1) { Start(); } else { Hit(k == kVolDown ? 0 : (k == kBoot ? 1 : 3)); } }

   private:
    struct T { float y; int8_t lane; bool on; };
    void Start() { for (auto& t : t_) { t.on = false; } idx_ = 0; song_ = (song_ + 1) % 3; gap_ = 0; score_ = 0U; combo_ = 0; miss_ = 0; speed_ = 3.0F; state_ = 1; }
    void Step() {
        speed_ += 0.0012F;
        if (--gap_ <= 0) { gap_ = 12 + static_cast<int32_t>(a_.random().Below(3U)) * 6; for (auto& t : t_) { if (!t.on) { t = {-20.0F, static_cast<int8_t>(a_.random().Below(4U)), true}; break; } } }
        for (auto& t : t_) { if (t.on) { t.y += speed_; if (t.y > static_cast<float>(kLine + 26)) { t.on = false; combo_ = 0; if (++miss_ >= 6) { state_ = 2; if (score_ > best_) { best_ = score_; p_.Set("best", best_); } Beep(a_, 120U, 300U); } } } }
    }
    void Hit(int32_t lane) {
        T* found = nullptr; for (auto& t : t_) { if (t.on && t.lane == lane && m::Abs(t.y - static_cast<float>(kLine)) < 30.0F && (found == nullptr || t.y > found->y)) { found = &t; } }
        if (found == nullptr) { combo_ = 0; return; }
        found->on = false; ++combo_; score_ += 10U + static_cast<uint32_t>(combo_) * 2U; fx_ = 6; lane_ = lane;
        Beep(a_, kHz[kSong[song_][idx_ % 32]], 110U, 240U, W::kTriangle); ++idx_;
    }
    void Draw() {
        c_.Begin();
        const Color k[] = {Rgb(255,110,130), Rgb(255,200,90), Rgb(110,230,170), Rgb(110,180,255)};
        for (int32_t i = 1; i < 4; ++i) { c_.Fill(i * kLaneW, 0, 1, kH, kDim); }
        c_.Fill(0, kLine, kW, 3, kWhite);
        if (fx_ > 0) { c_.Fill(lane_ * kLaneW + 1, kLine - 16, kLaneW - 1, 36, k[lane_], 120U); }
        for (const auto& t : t_) { if (t.on) { c_.Round(t.lane * kLaneW + 6, static_cast<int32_t>(t.y) - 12, kLaneW - 12, 24, 6, k[t.lane]); } }
        c_.Number(8, 6, static_cast<int32_t>(score_), kWhite, kMedium); c_.Number(kW - 40, 6, combo_, Rgb(255, 214, 110), kMedium);
        if (state_ == 0) { Banner(c_, "BEAT TILES", "Tap the lanes on the line", Rgb(110, 180, 255)); } if (state_ == 2) { Banner(c_, "SIX MISSES", "Tap to play again", Rgb(255, 110, 130)); }
        c_.Present();
    }
    Application& a_; Canvas c_; Prefs p_; T t_[10]{}; float speed_{}; int32_t state_{}, gap_{}, idx_{}, song_{}, combo_{}, miss_{}, fx_{}, lane_{}; uint32_t score_{}, best_{};
};
}  // namespace
PORTKIT_MAIN(Game)
