// SPDX-License-Identifier: Apache-2.0
//
// Call: rings the owner's iPhone. The call server sends the phone a VoIP push;
// answering it joins the same LiveKit room and the two talk through the
// Gotchi's speaker and microphones.
//
// Controls: BOOT (or tap) calls, and hangs up while a call runs.
#include <stdint.h>

#include "sdk/micropixel.hpp"

namespace {

using micropixel::CallEndReason;
using micropixel::CallPhase;
using micropixel::Color;
using micropixel::Duration;
using micropixel::SystemFont;

// The call server maps this name to the phone's push token.
constexpr const char* kCallee = "iphone";

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);
constexpr Color kBad = Color::Rgb(240U, 72U, 88U);

const char* EndText(CallEndReason reason) {
    switch (reason) {
        case CallEndReason::kHungUp:
            return "Call ended";
        case CallEndReason::kRemoteHungUp:
            return "They hung up";
        case CallEndReason::kNoAnswer:
            return "No answer";
        case CallEndReason::kFailed:
            return "Call failed";
        default:
            return "Ready";
    }
}

class CallApp final {
   public:
    explicit CallApp(micropixel::Application& app)
        : app_(app),
          call_(app.call()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(1000U)).value()) {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        const int32_t margin = width * 5 / 100;
        (void)scene_.CreateLabel({margin, height * 3 / 100}, "Call iPhone", kText, SystemFont::kMedium).value();
        status_ = scene_.CreateLabel({margin, height * 30 / 100}, "", kText, SystemFont::kLarge).value();
        detail_ = scene_.CreateLabel({margin, height * 50 / 100}, "", kMuted, SystemFont::kMedium).value();
        hint_ = scene_.CreateLabel({margin, height * 87 / 100}, "", kMuted, SystemFont::kSmall).value();
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (event.call() != nullptr || event.TimerFrom(tick_) != nullptr) {
            Refresh();
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown && key->code() == micropixel::KeyCode::kConfirm) {
                Primary();
            }
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kUp) {
                Primary();
            }
        } else if (event.type() == micropixel::EventType::kStop) {
            (void)call_.HangUp();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    void Primary() {
        const auto state = call_.State();
        if (!state) {
            return;
        }
        if (state->phase == CallPhase::kIdle) {
            if (!call_.Dial(kCallee)) {
                app_.log().Warning("dial refused");
            }
        } else {
            (void)call_.HangUp();
        }
        Refresh();
    }

    void Refresh() {
        const auto state = call_.State();
        if (!state || !state->available) {
            status_.SetText("Not set up");
            status_.SetColor(kBad);
            detail_.SetText("Needs Wi-Fi and a call server");
            hint_.SetText("");
            Present();
            return;
        }
        switch (state->phase) {
            case CallPhase::kIdle:
                status_.SetText(EndText(state->end_reason));
                status_.SetColor(state->end_reason == CallEndReason::kFailed ? kBad : kText);
                detail_.SetText("");
                hint_.SetText("BOOT or tap: call");
                break;
            case CallPhase::kDialing:
                status_.SetText("Calling...");
                status_.SetColor(kWarn);
                detail_.SetText("");
                hint_.SetText("BOOT or tap: cancel");
                break;
            case CallPhase::kRinging:
                status_.SetText("Ringing...");
                status_.SetColor(kWarn);
                detail_.SetText("Waiting for iPhone");
                hint_.SetText("BOOT or tap: cancel");
                break;
            case CallPhase::kConnected: {
                status_.SetText("Connected");
                status_.SetColor(kGood);
                const uint32_t seconds = state->connected_ms / 1000U;
                const char clock[6] = {
                    static_cast<char>('0' + seconds / 600U % 10U), static_cast<char>('0' + seconds / 60U % 10U), ':',
                    static_cast<char>('0' + seconds % 60U / 10U),  static_cast<char>('0' + seconds % 10U),       '\0'};
                detail_.SetText(clock);
                hint_.SetText("BOOT or tap: hang up");
                break;
            }
        }
        Present();
    }

    void Present() { (void)app_.renderer().Present(scene_); }

    micropixel::Application& app_;
    micropixel::Call call_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::LabelNode status_;
    micropixel::LabelNode detail_;
    micropixel::LabelNode hint_;
};

}  // namespace

int main() {
    micropixel::Application app;
    CallApp call_app(app);
    app.log().Info("Call ready");
    app.Run([&call_app](const micropixel::Event& event) { return call_app.Handle(event); });
    return 0;
}
