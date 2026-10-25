// SPDX-License-Identifier: Apache-2.0
//
// Phone Cam: shows a phone's camera live. The Gotchi serves a camera page on
// the Wi-Fi network; opening it in Safari on a phone on the same Wi-Fi streams
// the phone camera here. While video arrives the frames fill the screen; when
// it stops for two seconds the address screen comes back.
#include <stdint.h>

#include "sdk/micropixel.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kAccent = Color::Rgb(250U, 122U, 26U);
constexpr Color kBad = Color::Rgb(240U, 72U, 88U);

class PhoneCamApp final {
   public:
    explicit PhoneCamApp(micropixel::Application& app)
        : app_(app),
          video_(app.remote_video()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(500U)).value()) {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        const int32_t margin = width * 5 / 100;
        (void)scene_.CreateLabel({margin, height * 4 / 100}, "Phone Cam", kText, SystemFont::kMedium).value();
        status_ = scene_.CreateLabel({margin, height * 24 / 100}, "", kText, SystemFont::kMedium).value();
        url_ = scene_.CreateLabel({margin, height * 42 / 100}, "", kAccent, SystemFont::kLarge).value();
        detail_ = scene_.CreateLabel({margin, height * 62 / 100}, "", kMuted, SystemFont::kSmall).value();
        hint_ = scene_.CreateLabel({margin, height * 74 / 100}, "", kMuted, SystemFont::kSmall).value();
        started_ = video_.Start().has_value();
        if (!started_) {
            app_.log().Warning("remote video start failed");
        }
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (event.remote_video() != nullptr) {
            frame_pending_ = true;
            if (!surface_.valid()) {
                OpenSurface();
            }
            DrawFrame();
        } else if (surface_.valid() && event.ReleasedFrom(surface_) != nullptr) {
            DrawFrame();
        } else if (event.TimerFrom(tick_) != nullptr) {
            Refresh();
        } else if (event.type() == micropixel::EventType::kResume) {
            // Suspend stopped the page server; Resume restarts it.
            Refresh();
        } else if (event.type() == micropixel::EventType::kStop) {
            surface_.Reset();
            (void)video_.Stop();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    void OpenSurface() {
        auto created = app_.renderer().CreateGuestSurface(2U);
        if (!created) {
            app_.log().Error("guest surface unavailable");
            return;
        }
        surface_ = static_cast<micropixel::GuestSurface&&>(*created);
    }

    void CloseSurface() {
        surface_.Reset();
        frame_pending_ = false;
        (void)app_.renderer().Present(scene_);
    }

    // Reads the newest frame into a free buffer. With every buffer in flight
    // the frame waits for the next kSurfaceReleased.
    void DrawFrame() {
        uint32_t index = 0U;
        if (!frame_pending_ || !surface_.valid() || !surface_.AcquireFree(index)) {
            return;
        }
        uint16_t* pixels = surface_.Buffer(index);
        auto read =
            video_.ReadFrame(pixels, surface_.buffer_width(), surface_.buffer_height(), surface_.rgb565_byte_swapped());
        frame_pending_ = false;
        if (read) {
            (void)surface_.Present(index);
        }
    }

    void Refresh() {
        const auto state = video_.State();
        if (surface_.valid()) {
            if (state && !state->sending) {
                CloseSurface();
            } else {
                DrawFrame();
                return;
            }
        }
        if (!state || !state->available) {
            ShowMessage("Not supported", kBad, "", "This Gotchi firmware has no", "Remote Video service.");
        } else if (!started_ || !state->started) {
            ShowMessage("Could not start", kBad, "", "The camera page did not start.", "Restart the app.");
        } else if (!state->network) {
            ShowMessage("Join Wi-Fi first", kBad, "", "Connect the Gotchi to Wi-Fi", "in Settings, then come back.");
        } else {
            ShowMessage(state->frames_received == 0U ? "On your phone, open" : "Paused. Open again", kText, state->url,
                        "Same Wi-Fi. Safari warns about", "the certificate: tap Show Details, visit.");
        }
    }

    void ShowMessage(const char* status, Color status_color, const char* url, const char* detail, const char* hint) {
        status_.SetText(status);
        status_.SetColor(status_color);
        url_.SetText(url);
        detail_.SetText(detail);
        hint_.SetText(hint);
        (void)app_.renderer().Present(scene_);
    }

    micropixel::Application& app_;
    micropixel::RemoteVideo video_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::GuestSurface surface_;
    micropixel::LabelNode status_;
    micropixel::LabelNode url_;
    micropixel::LabelNode detail_;
    micropixel::LabelNode hint_;
    bool started_{};
    bool frame_pending_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    PhoneCamApp phone_cam(app);
    app.log().Info("Phone Cam ready");
    app.Run([&phone_cam](const micropixel::Event& event) { return phone_cam.Handle(event); });
    return 0;
}
