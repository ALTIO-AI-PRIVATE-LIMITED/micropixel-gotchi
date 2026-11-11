// SPDX-License-Identifier: Apache-2.0
//
// Gotchi Info: battery, charging state, screen, uptime, tilt and sound level on one screen.
#include "apps/portkit/portkit.hpp"
namespace {
using namespace portkit;
namespace m = micropixel::math;
using W = micropixel::Waveform;
class Game final {
   public:
    explicit Game(Application& a) : a_(a), c_(a, Rgb(14, 20, 30)), t_(a), mic_(a), ck_(a) { mic_.Start(); Read(); }
    void Tick(uint32_t ms) { t_.Poll(); el_ += ms; if (el_ >= 2000U) { el_ = 0U; Read(); } Draw(); }
    void Mic() { int16_t b[256]; const uint32_t n = mic_.Read(b, 256U); int32_t top = 0; for (uint32_t i = 0U; i < n; ++i) { top = m::Max(top, m::Abs(static_cast<int32_t>(b[i]))); } lvl_ = m::Max(top, lvl_ * 9 / 10); }
    void Tap(int32_t, int32_t) { Read(); Beep(a_, 700U, 40U); }

   private:
    void Read() { auto list = a_.devices().List(micropixel::DeviceKind::kPower); if (!list || list->size() == 0U) { has_ = false; return; } auto st = a_.power_info().Get((*list)[0]); has_ = st.has_value(); if (has_) { pct_ = st->battery_percent; bat_ = st->has_battery; chg_ = st->charging; ext_ = st->external_connected; } }
    void Row(int32_t y, const char* name, const char* value) { c_.Text(14, y, name, kGrey); c_.Text(120, y, value, kWhite); }
    void Draw() {
        c_.Begin();
        c_.Text(14, 8, "This Gotchi", Rgb(120, 200, 255), kMedium);
        c_.Round(196, 12, 76, 28, 5, kDim); c_.Fill(272, 20, 5, 12, kDim); const int32_t w = bat_ ? static_cast<int32_t>(pct_) * 70 / 100 : 0; c_.Fill(199, 15, w, 22, pct_ < 20 ? Rgb(255, 90, 90) : Rgb(110, 230, 140));
        char p[12]; Canvas::Format(p, static_cast<int32_t>(pct_)); int32_t n = 0; while (p[n] != '\0') { ++n; } p[n++] = '%'; p[n] = '\0';
        Row(52, "Battery", !has_ ? "Not reported" : (bat_ ? p : "None")); Row(74, "Power", chg_ ? "Charging" : (ext_ ? "USB power" : "On battery"));
        Row(96, "Screen", "296 x 240"); char up[16]; const uint32_t s = ck_.Now() / 1000U; Canvas::Format(up, static_cast<int32_t>(s)); int32_t q = 0; while (up[q] != '\0') { ++q; } up[q++] = ' '; up[q++] = 's'; up[q] = '\0'; Row(118, "App open", up);
        c_.Text(14, 142, "Tilt", kGrey); c_.Fill(120, 150, 160, 2, kDim); c_.Disc(200 + static_cast<int32_t>(t_.x() * 76.0F), 151, 6, Rgb(255, 180, 90));
        c_.Text(14, 168, "Forward", kGrey); c_.Fill(120, 176, 160, 2, kDim); c_.Disc(200 + static_cast<int32_t>(t_.y() * 76.0F), 177, 6, Rgb(255, 180, 90));
        c_.Text(14, 196, "Sound", kGrey); c_.Fill(120, 200, 160, 8, Rgb(40, 46, 60)); c_.Fill(120, 200, m::Min(lvl_ / 80, 160), 8, Rgb(120, 230, 180));
        c_.Present();
    }
    Application& a_; Canvas c_; Tilt t_; portkit::Mic mic_; Millis ck_; uint32_t el_{}; uint8_t pct_{}; bool has_{}, bat_{}, chg_{}, ext_{}; int32_t lvl_{};
};
}  // namespace
PORTKIT_MAIN(Game)
