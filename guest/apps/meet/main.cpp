// SPDX-License-Identifier: Apache-2.0
//
// Meet: your Google Meet meetings, audio only. Lists the call server's
// upcoming meetings and joins each one when it starts: a meeting bot joins the
// Meet and the call carries its audio to the Gotchi's speaker and microphones.
// Meet calls start muted.
//
// Controls: in the list, VOL- / VOL+ pick a meeting and BOOT or a tap joins it
// now. In a meeting, VOL+ mutes and unmutes; hold VOL- (or press BOOT) to leave.
#include <stdint.h>

#include "sdk/micropixel.hpp"

namespace {

using micropixel::CallEndReason;
using micropixel::CallMeeting;
using micropixel::CallMeetings;
using micropixel::CallPhase;
using micropixel::Color;
using micropixel::Duration;
using micropixel::KeyCode;
using micropixel::KeyPhase;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kHighlight = Color::Rgb(40U, 46U, 60U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kDim = Color::Rgb(150U, 158U, 172U);
constexpr Color kAccent = Color::Rgb(252U, 140U, 0U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);
constexpr Color kBad = Color::Rgb(240U, 72U, 88U);

constexpr uint32_t kRows = 3U;
constexpr uint32_t kTitleChars = 22U;
constexpr uint64_t kLeaveHoldUs = 1000000U;
constexpr uint32_t kEndShowTicks = 3U;
constexpr uint32_t kRemembered = 8U;

void Copy(char* destination, uint32_t capacity, const char* source) {
    uint32_t i = 0U;
    for (; i + 1U < capacity && source[i] != '\0'; ++i) {
        destination[i] = source[i];
    }
    destination[i] = '\0';
}

bool Same(const char* a, const char* b) {
    for (uint32_t i = 0U;; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

// Cuts `title` to the row width with "..".
void Shorten(char (&out)[kTitleChars + 1U], const char* title) {
    Copy(out, sizeof(out), title);
    uint32_t length = 0U;
    while (title[length] != '\0') {
        ++length;
    }
    if (length > kTitleChars) {
        out[kTitleChars - 2U] = '.';
        out[kTitleChars - 1U] = '.';
    }
}

const char* EndText(CallEndReason reason) {
    switch (reason) {
        case CallEndReason::kHungUp:
            return "Left meeting";
        case CallEndReason::kRemoteHungUp:
            return "Meeting over";
        case CallEndReason::kNoAnswer:
            return "Not let in";
        case CallEndReason::kFailed:
            return "Couldn't join";
        default:
            return "";
    }
}

class MeetApp final {
   public:
    explicit MeetApp(micropixel::Application& app)
        : app_(app),
          call_(app.call()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(1000U)).value()) {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        margin_ = width * 5 / 100;
        row_top_ = height * 17 / 100;
        row_height_ = height * 22 / 100;
        highlight_ = scene_.CreateShape({0, row_top_, width, row_height_}, kBackground).value();
        header_ = scene_.CreateLabel({margin_, height * 3 / 100}, "Meetings", kAccent, SystemFont::kMedium).value();
        for (uint32_t row = 0U; row < kRows; ++row) {
            const int32_t y = row_top_ + static_cast<int32_t>(row) * row_height_;
            row_time_[row] = scene_.CreateLabel({margin_, y + 2}, "", kAccent, SystemFont::kSmall).value();
            row_title_[row] = scene_.CreateLabel({margin_, y + row_height_ / 2}, "", kText, SystemFont::kMedium).value();
        }
        status_ = scene_.CreateLabel({margin_, height * 30 / 100}, "", kText, SystemFont::kLarge).value();
        detail_ = scene_.CreateLabel({margin_, height * 52 / 100}, "", kDim, SystemFont::kMedium).value();
        hint_ = scene_.CreateLabel({margin_, height * 87 / 100}, "", kDim, SystemFont::kSmall).value();
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (event.TimerFrom(tick_) != nullptr) {
            if (end_ticks_ > 0U) {
                --end_ticks_;
            }
            Refresh();
        } else if (event.call() != nullptr) {
            if (event.call()->phase() == CallPhase::kIdle) {
                end_ticks_ = kEndShowTicks;
            }
            Refresh();
        } else if (const auto* key = event.key()) {
            OnKey(*key);
        } else if (const auto* touch = event.touch()) {
            // Taps only join: a stray touch must never drop you from a meeting.
            if (touch->phase() == micropixel::TouchPhase::kUp && Idle()) {
                JoinSelected();
            }
        } else if (event.type() == micropixel::EventType::kStop) {
            (void)call_.HangUp();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    bool Idle() const {
        const auto state = call_.State();
        return state && state->phase == CallPhase::kIdle;
    }

    void OnKey(const micropixel::KeyEvent& key) {
        const bool idle = Idle();
        const uint64_t now_us = key.timestamp().microseconds();
        if (key.code() == KeyCode::kLeft) {
            if (key.phase() == KeyPhase::kDown) {
                vol_down_since_us_ = now_us;
            } else if (!idle && vol_down_since_us_ != 0U && now_us - vol_down_since_us_ >= kLeaveHoldUs) {
                vol_down_since_us_ = 0U;  // Held long enough (repeat or release): leave.
                (void)call_.HangUp();
                Refresh();
                return;
            }
            if (key.phase() == KeyPhase::kUp || key.phase() == KeyPhase::kCancel) {
                vol_down_since_us_ = 0U;
            }
        }
        if (key.phase() != KeyPhase::kDown) {
            return;
        }
        if (idle) {
            if (key.code() == KeyCode::kRight && selected_ > 0U) {
                --selected_;
            } else if (key.code() == KeyCode::kLeft && selected_ + 1U < meetings_.count) {
                ++selected_;
            } else if (key.code() == KeyCode::kConfirm) {
                JoinSelected();
                return;
            }
        } else if (key.code() == KeyCode::kRight) {
            const auto state = call_.State();
            if (state) {
                (void)call_.SetMuted(!state->muted);
            }
        } else if (key.code() == KeyCode::kConfirm) {
            (void)call_.HangUp();
        }
        Refresh();
    }

    void Join(const CallMeeting& meeting) {
        Remember(meeting.key);
        Copy(joined_title_, sizeof(joined_title_), meeting.title);
        char callee[micropixel::Call::kMaxCalleeBytes + 1U] = "meet:";
        Copy(callee + 5, sizeof(callee) - 5U, meeting.key);
        if (!call_.Dial(callee)) {
            app_.log().Warning("dial refused");
        }
    }

    void JoinSelected() {
        if (selected_ < meetings_.count) {
            Join(meetings_.meetings[selected_]);
            Refresh();
        }
    }

    // Meetings already joined (or tried), so auto-join doesn't rejoin after you leave.
    bool Remembered(const char* key) const {
        for (const auto& seen : joined_) {
            if (Same(seen, key)) {
                return true;
            }
        }
        return false;
    }

    void Remember(const char* key) {
        if (!Remembered(key)) {
            Copy(joined_[joined_next_], sizeof(joined_[0]), key);
            joined_next_ = (joined_next_ + 1U) % kRemembered;
        }
    }

    // Joins the first meeting that has started and wasn't joined yet.
    bool AutoJoin() {
        for (uint32_t i = 0U; i < meetings_.count; ++i) {
            const CallMeeting& meeting = meetings_.meetings[i];
            if (meeting.start_s <= meetings_.now_s && meetings_.now_s < meeting.end_s && !Remembered(meeting.key)) {
                selected_ = i;
                Join(meeting);
                return true;
            }
        }
        return false;
    }

    void Refresh() {
        const auto state = call_.State();
        if (!state || !state->available) {
            ShowMessage("Not set up", kBad, "Needs Wi-Fi and a call server");
            return;
        }
        if (state->phase == CallPhase::kIdle) {
            auto meetings = call_.Meetings();
            if (meetings) {
                meetings_ = *meetings;
                loaded_ = true;
                if (selected_ >= meetings_.count) {
                    selected_ = meetings_.count > 0U ? meetings_.count - 1U : 0U;
                }
            }
            if (end_ticks_ == 0U && loaded_ && AutoJoin()) {
                return;  // The DIALING event redraws.
            }
            if (end_ticks_ > 0U && state->end_reason != CallEndReason::kNone) {
                ShowMessage(EndText(state->end_reason), state->end_reason == CallEndReason::kFailed ? kBad : kText, "");
            } else {
                ShowList();
            }
            return;
        }
        ShowCall(*state);
    }

    void ClearRows() {
        highlight_.SetColor(kBackground);
        for (uint32_t row = 0U; row < kRows; ++row) {
            row_time_[row].SetText("");
            row_title_[row].SetText("");
        }
    }

    void ShowMessage(const char* status, Color color, const char* detail) {
        ClearRows();
        header_.SetText("Meet");
        status_.SetText(status);
        status_.SetColor(color);
        detail_.SetText(detail);
        hint_.SetText("");
        Present();
    }

    void ShowList() {
        status_.SetText("");
        detail_.SetText("");
        header_.SetText(meetings_.stale ? "Meetings (offline)" : "Meetings");
        if (!loaded_ || meetings_.count == 0U) {
            ClearRows();
            status_.SetText(loaded_ ? "No meetings" : "Loading...");
            status_.SetColor(kText);
            detail_.SetText(loaded_ ? "in the next 24 hours" : "");
            hint_.SetText("");
            Present();
            return;
        }
        const uint32_t first = selected_ + 1U > kRows ? selected_ + 1U - kRows : 0U;
        for (uint32_t row = 0U; row < kRows; ++row) {
            const uint32_t index = first + row;
            if (index >= meetings_.count) {
                row_time_[row].SetText("");
                row_title_[row].SetText("");
                continue;
            }
            const CallMeeting& meeting = meetings_.meetings[index];
            const bool live = meeting.start_s <= meetings_.now_s && meetings_.now_s < meeting.end_s;
            row_time_[row].SetText(live ? "Now" : meeting.label);
            row_time_[row].SetColor(live ? kGood : kAccent);
            char title[kTitleChars + 1U];
            Shorten(title, meeting.title);
            row_title_[row].SetText(title);
            row_title_[row].SetColor(index == selected_ ? kText : kDim);
        }
        const int32_t y = row_top_ + static_cast<int32_t>(selected_ - first) * row_height_;
        highlight_.SetRect({0, y, static_cast<int32_t>(info_.width()), row_height_});
        highlight_.SetColor(kHighlight);
        hint_.SetText("VOL keys pick, BOOT or tap joins");
        Present();
    }

    void ShowCall(const micropixel::CallState& state) {
        ClearRows();
        char title[kTitleChars + 1U];
        Shorten(title, joined_title_);
        header_.SetText(title);
        switch (state.phase) {
            case CallPhase::kDialing:
                status_.SetText("Joining...");
                status_.SetColor(kWarn);
                detail_.SetText("");
                break;
            case CallPhase::kRinging:
                status_.SetText("Waiting...");
                status_.SetColor(kWarn);
                detail_.SetText("to be let in");
                break;
            default: {
                status_.SetText(state.muted ? "Muted" : "Live");
                status_.SetColor(state.muted ? kBad : kGood);
                const uint32_t seconds = state.connected_ms / 1000U;
                const char clock[6] = {
                    static_cast<char>('0' + seconds / 600U % 10U), static_cast<char>('0' + seconds / 60U % 10U), ':',
                    static_cast<char>('0' + seconds % 60U / 10U),  static_cast<char>('0' + seconds % 10U),       '\0'};
                detail_.SetText(clock);
                break;
            }
        }
        hint_.SetText("VOL+ mute/unmute, hold VOL- leave");
        Present();
    }

    void Present() { (void)app_.renderer().Present(scene_); }

    micropixel::Application& app_;
    micropixel::Call call_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::ShapeNode highlight_;
    micropixel::LabelNode header_;
    micropixel::LabelNode row_time_[kRows];
    micropixel::LabelNode row_title_[kRows];
    micropixel::LabelNode status_;
    micropixel::LabelNode detail_;
    micropixel::LabelNode hint_;
    int32_t margin_{};
    int32_t row_top_{};
    int32_t row_height_{};

    CallMeetings meetings_{};
    bool loaded_{};
    uint32_t selected_{};
    uint32_t end_ticks_{};
    uint64_t vol_down_since_us_{};
    char joined_title_[48]{};
    char joined_[kRemembered][16]{};
    uint32_t joined_next_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    MeetApp meet_app(app);
    app.log().Info("Meet ready");
    app.Run([&meet_app](const micropixel::Event& event) { return meet_app.Handle(event); });
    return 0;
}
