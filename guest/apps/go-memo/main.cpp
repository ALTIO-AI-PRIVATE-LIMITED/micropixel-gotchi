// SPDX-License-Identifier: Apache-2.0
//
// Memo: hold to record eight seconds, tap to hear it back. It stays in memory until you close the app.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
constexpr int32_t kMax = 64000; int8_t clip[kMax];
class Game final {
   public:
    explicit Game(Application& a) : c_(a, Rgb(40, 20, 28)), mic_(a), spk_(a) {}
    Speaker& speaker() { return spk_; }
    void Tick(uint32_t) { Pump(); Draw(); }
    void Mic() { if (!rec_) { int16_t b[64]; (void)mic_.Read(b, 64U); return; } int16_t b[320]; const uint32_t n = mic_.Read(b, 320U); for (uint32_t i = 0U; i + 1U < n && len_ < kMax; i += 2U) { clip[len_++] = static_cast<int8_t>(b[i] / 256); } if (len_ >= kMax) { StopRec(); } }
    void Feed() { Pump(); }
    void Press(int32_t, int32_t y) { if (y > 130) { spk_.Close(); pl_ = false; if (mic_.Start()) { rec_ = true; len_ = 0; } } else if (len_ > 400) { if (pl_) { pl_ = false; spk_.Close(); } else if (spk_.Open(8000U)) { pos_ = 0; pl_ = true; Pump(); } } }
    void Tap(int32_t, int32_t) { StopRec(); }
    void Button(KeyCode k) { if (k == kBoot) { if (rec_) { StopRec(); } else { Press(0, 200); } } }

   private:
    void StopRec() { if (rec_) { rec_ = false; mic_.Stop(); } }
    void Pump() { if (!pl_) { return; } for (int32_t g = 0; g < 6; ++g) { const int32_t n = m::Min(len_ - pos_, 256); if (n <= 0) { pl_ = false; spk_.Close(); return; } int16_t out[256]; for (int32_t i = 0; i < n; ++i) { out[i] = static_cast<int16_t>(clip[pos_ + i] * 240); } const auto took = spk_.Write(out, static_cast<uint32_t>(n)); pos_ += static_cast<int32_t>(took); if (took < static_cast<uint32_t>(n)) { return; } } }
    void Draw() {
        c_.Begin();
        c_.Round(10, 10, kW - 20, 116, 12, Rgb(70, 30, 50));
        const int32_t span = len_ / 260 + 1; for (int32_t x = 0; x < 260 && x * span < len_; ++x) { int32_t a = 0; for (int32_t i = 0; i < span; i += m::Max(span / 8, 1)) { a = m::Max(a, m::Abs(static_cast<int32_t>(clip[x * span + i]))); } a = m::Min(a * 3 / 4, 44); c_.Fill(18 + x, 68 - a, 1, 2 * a + 1, Rgb(255, 170, 130)); }
        if (pl_) { c_.Fill(18 + pos_ * 260 / m::Max(len_, 1), 16, 2, 104, kWhite); }
        c_.Text(kW / 2, 14, len_ <= 400 ? "Nothing recorded yet" : (pl_ ? "Playing, tap to stop" : "Tap here to play"), kWhite, kSmall, true);
        c_.Round(10, 136, kW - 20, 96, 12, rec_ ? Rgb(230, 60, 80) : Rgb(120, 50, 70)); c_.Disc(kW / 2, 172, 18, rec_ ? kWhite : Rgb(240, 90, 90)); c_.Text(kW / 2, 202, rec_ ? "Recording" : "Hold to record", kWhite, kSmall, true);
        c_.Present();
    }
    Canvas c_; portkit::Mic mic_; Speaker spk_; int32_t len_{}, pos_{}; bool rec_{}, pl_{};
};
}  // namespace
PORTKIT_MAIN(Game)
