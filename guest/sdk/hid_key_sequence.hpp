#ifndef MICROPIXEL_SDK_HID_KEY_SEQUENCE_HPP
#define MICROPIXEL_SDK_HID_KEY_SEQUENCE_HPP

#include <stdint.h>

#include <array>
#include <initializer_list>
#include <optional>

#include "sdk/event.hpp"
#include "sdk/hid_keyboard.hpp"
#include "sdk/timer.hpp"
#include "sdk/types.hpp"

namespace micropixel {

// Timed script of HidKeyboard presses and releases (shortcuts, typed words,
// media keys) for the single-threaded event loop. Queue steps, call Start(),
// then pass every Event to Handle(); pauses wait on a one-shot Timer.
class HidKeySequence final {
   public:
    static constexpr uint32_t kCapacity = 64U;

    enum class Status : uint8_t {
        kIdle,     // Nothing queued or running.
        kRunning,  // Waiting for a pause to elapse.
        kDone,     // The last step was sent.
        kFailed,   // A step was rejected (usually: not connected); keys were lifted.
    };

    HidKeySequence(HidKeyboard keyboard, Timers timers) : keyboard_(keyboard), timers_(timers) {}

    // Queue calls return false when the script is full or already running.
    bool Key(HidKeyUsage usage, bool pressed, Duration pause_after = {}) {
        return Queue(Step{static_cast<uint16_t>(usage), false, pressed, pause_after});
    }
    bool Consumer(HidConsumerUsage usage, bool pressed, Duration pause_after = {}) {
        return Queue(Step{static_cast<uint16_t>(usage), true, pressed, pause_after});
    }
    // Presses `chord` in order, pauses `hold`, releases it in reverse order.
    bool Chord(std::initializer_list<HidKeyUsage> chord, Duration hold, Duration pause_after = {}) {
        if (!Writable() || count_ + chord.size() * 2U > kCapacity) {
            return false;
        }
        uint32_t index = 0U;
        for (HidKeyUsage usage : chord) {
            (void)Key(usage, true, ++index == chord.size() ? hold : Duration{});
        }
        for (auto usage = chord.end(); usage != chord.begin();) {
            --usage;
            (void)Key(*usage, false, usage == chord.begin() ? pause_after : Duration{});
        }
        return true;
    }
    // Types lowercase letters and spaces.
    bool Type(const char* text, Duration between = Duration::Milliseconds(15U)) {
        for (; *text != '\0'; ++text) {
            const HidKeyUsage usage = *text == ' ' ? HidKeyUsage::kSpace : HidLetter(*text);
            if (!Key(usage, true, between) || !Key(usage, false, between)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool running() const { return started_ && next_ < count_; }

    Status Start() {
        if (count_ == 0U || started_) {
            return running() ? Status::kRunning : Status::kIdle;
        }
        started_ = true;
        return Advance();
    }

    // Returns kIdle for events that are not this sequence's timer.
    Status Handle(const Event& event) {
        if (!timer_ || event.TimerFrom(*timer_) == nullptr) {
            return Status::kIdle;
        }
        return Advance();
    }

    // Abandons the script and lifts every key it pressed.
    void Cancel() {
        for (uint32_t index = 0U; index < next_; ++index) {
            if (steps_[index].pressed) {
                (void)Send(Step{steps_[index].usage, steps_[index].consumer, false, {}});
            }
        }
        Clear();
    }

   private:
    struct Step final {
        uint16_t usage{};
        bool consumer{};
        bool pressed{};
        Duration pause_after{};
    };

    // A finished script is replaced by the next one; a running one is not.
    bool Writable() {
        if (running()) {
            return false;
        }
        if (started_) {
            Clear();
        }
        return true;
    }

    bool Queue(Step step) {
        if (!Writable() || count_ >= kCapacity) {
            return false;
        }
        steps_[count_++] = step;
        return true;
    }

    [[nodiscard]] bool Send(const Step& step) const {
        return step.consumer
                   ? static_cast<bool>(keyboard_.Consumer(static_cast<HidConsumerUsage>(step.usage), step.pressed))
                   : static_cast<bool>(keyboard_.Key(static_cast<HidKeyUsage>(step.usage), step.pressed));
    }

    Status Advance() {
        timer_.reset();
        while (next_ < count_) {
            const Step& step = steps_[next_++];
            if (!Send(step)) {
                Cancel();
                return Status::kFailed;
            }
            if (step.pause_after != Duration{} && next_ < count_) {
                auto timer = timers_.After(step.pause_after);
                if (timer) {
                    timer_.emplace(static_cast<Timer&&>(*timer));
                    return Status::kRunning;
                }
            }
        }
        return Status::kDone;
    }

    void Clear() {
        started_ = false;
        count_ = 0U;
        next_ = 0U;
        timer_.reset();
    }

    HidKeyboard keyboard_;
    Timers timers_;
    std::array<Step, kCapacity> steps_{};
    uint32_t count_{};
    uint32_t next_{};
    bool started_{};
    std::optional<Timer> timer_;
};

}  // namespace micropixel

#endif
