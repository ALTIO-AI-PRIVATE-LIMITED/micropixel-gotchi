// SPDX-License-Identifier: Apache-2.0
//
// Mic Test: records three seconds from the built-in microphone and plays them
// straight back through the speaker. Bring-up check for the Microphone
// service before the walkie-talkie uses it.
//
// Controls: tap the screen, BOOT or VOL+ to record.
#include <stdint.h>

#include <optional>

#include "sdk/micropixel.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::Rect;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kTrack = Color::Rgb(40U, 44U, 56U);
constexpr Color kRecord = Color::Rgb(240U, 72U, 88U);
constexpr Color kPlay = Color::Rgb(72U, 200U, 120U);

constexpr uint32_t kSampleRate = 16000U;
constexpr uint32_t kRecordSamples = kSampleRate * 3U;
constexpr uint32_t kStreamCapacity = 4096U;
constexpr uint32_t kStreamLowWater = 1024U;

int16_t recording[kRecordSamples];

// Appends the decimal form of `value` at `out`; returns the end.
char* AppendNumber(char* out, uint32_t value) {
    char digits[10];
    uint32_t count = 0U;
    do {
        digits[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U) {
        *out++ = digits[--count];
    }
    *out = '\0';
    return out;
}

char* AppendText(char* out, const char* text) {
    while (*text != '\0') {
        *out++ = *text++;
    }
    *out = '\0';
    return out;
}

enum class Mode : uint8_t { kIdle, kCue, kRecording, kPlaying };

// Playback lifts the take so its loudest sample reaches kTargetPeak, by at
// most kMaxBoost: a speaking voice records ~30 dB under full scale.
constexpr int32_t kTargetPeak = 26000;
constexpr int32_t kMaxBoost = 16;
// Diagnostic: log the raw take as hex (160 samples per line) after recording.
constexpr bool kDumpTake = false;

class MicTestApp final {
   public:
    explicit MicTestApp(micropixel::Application& app)
        : app_(app),
          microphone_(app.microphone()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        const int32_t margin = width_ * 6 / 100;
        (void)scene_.CreateLabel({margin, height_ * 5 / 100}, "Mic Test", kText, SystemFont::kMedium).value();
        status_ = scene_.CreateLabel({margin, height_ * 25 / 100}, "", kText, SystemFont::kLarge).value();
        detail_ = scene_.CreateLabel({margin, height_ * 42 / 100}, "", kMuted, SystemFont::kSmall).value();
        bar_track_ = {margin, height_ * 62 / 100, width_ - 2 * margin, height_ * 10 / 100};
        micropixel::RoundedRectStyle track{};
        track.fill = kTrack;
        track.radius = 8U;
        (void)scene_.CreateRoundedRect(bar_track_, track).value();
        micropixel::RoundedRectStyle bar{};
        bar.fill = kRecord;
        bar.radius = 8U;
        bar_ = scene_.CreateRoundedRect({bar_track_.x, bar_track_.y, 0, bar_track_.height}, bar).value();
        hint_ = scene_.CreateLabel({margin, height_ * 82 / 100}, "Tap, BOOT or VOL+ to record 3 s", kMuted,
                                   SystemFont::kSmall)
                    .value();

        const auto mic = microphone_.Info();
        if (!mic || !mic->available) {
            ShowStatus("No microphone", kRecord);
            detail_.SetText("Host has no Microphone service");
        } else {
            char* end = AppendText(detail_text_, "Ready: ");
            end = AppendNumber(end, mic->sample_rate);
            end = AppendText(end, " Hz, frame ");
            (void)AppendNumber(end, mic->frame_samples);
            detail_.SetText(detail_text_);
            ShowStatus("Ready", kText);
        }
        Present();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (const auto* captured = event.microphone()) {
            dropped_ += captured->dropped_samples();
            Capture();
        } else if (stream_ && event.LowWaterFrom(*stream_) != nullptr) {
            Feed();
        } else if (cue_ && event.TimerFrom(*cue_) != nullptr) {
            BeginCapture();
        } else if (drain_ && event.TimerFrom(*drain_) != nullptr) {
            FinishPlayback();
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kUp) {
                StartRecording();
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown &&
                (key->code() == micropixel::KeyCode::kConfirm || key->code() == micropixel::KeyCode::kRight)) {
                StartRecording();
            }
        } else if (event.type() == micropixel::EventType::kStop) {
            cue_.reset();
            (void)microphone_.Stop();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    void StartRecording() {
        if (mode_ != Mode::kIdle) {
            return;
        }
        recorded_ = 0U;
        dropped_ = 0U;
        peak_ = 0;
        // Start cue first; the microphone opens once it has finished so the
        // take holds only the voice.
        micropixel::Tone cue{};
        cue.frequency_hz = 880U;
        cue.duration = Duration::Milliseconds(150U);
        cue.volume_per_mille = 300U;
        (void)app_.audio().Play(cue);
        mode_ = Mode::kCue;
        cue_.emplace(app_.timers().After(Duration::Milliseconds(250U)).value());
        ShowStatus("Get ready...", kRecord);
        Present();
    }

    void BeginCapture() {
        cue_.reset();
        if (const auto started = microphone_.Start(); !started) {
            mode_ = Mode::kIdle;
            ShowStatus("Mic start failed", kRecord);
            Present();
            return;
        }
        mode_ = Mode::kRecording;
        started_at_ = app_.clock().Now();
        ShowStatus("Recording...", kRecord);
        detail_.SetText("Speak now");
        Present();
    }

    void Capture() {
        if (mode_ != Mode::kRecording) {
            return;
        }
        int32_t chunk_peak = 0;
        while (recorded_ < kRecordSamples) {
            uint32_t want = kRecordSamples - recorded_;
            if (want > micropixel::Microphone::kMaxReadSamples) {
                want = micropixel::Microphone::kMaxReadSamples;
            }
            const auto read = microphone_.Read(recording + recorded_, want);
            if (!read || *read == 0U) {
                break;
            }
            for (uint32_t index = 0U; index < *read; ++index) {
                int32_t sample = recording[recorded_ + index];
                sample = sample < 0 ? -sample : sample;
                chunk_peak = sample > chunk_peak ? sample : chunk_peak;
            }
            recorded_ += *read;
        }
        peak_ = chunk_peak > peak_ ? chunk_peak : peak_;
        // Logarithmic meter: room noise (~100) stays near empty, speech fills
        // most of it. 15 steps of 1.5x span 100..32767.
        int32_t steps = 0;
        for (int32_t threshold = 100; threshold <= chunk_peak && steps < 15; threshold = threshold * 3 / 2) {
            ++steps;
        }
        SetBar(steps * 32767 / 15, kRecord);
        if (recorded_ >= kRecordSamples) {
            (void)microphone_.Stop();
            const Duration elapsed = app_.clock().Now() - started_at_;
            if (kDumpTake) {
                DumpTake();
            }
            int32_t boost = peak_ > 0 ? kTargetPeak / peak_ : kMaxBoost;
            boost = boost < 1 ? 1 : (boost > kMaxBoost ? kMaxBoost : boost);
            for (uint32_t index = 0U; index < kRecordSamples; ++index) {
                int32_t value = static_cast<int32_t>(recording[index]) * boost;
                value = value > 32767 ? 32767 : (value < -32768 ? -32768 : value);
                recording[index] = static_cast<int16_t>(value);
            }
            char* end = AppendText(detail_text_, "Voice peak ");
            end = AppendNumber(end, static_cast<uint32_t>(peak_));
            end = AppendText(end, ", boost x");
            end = AppendNumber(end, static_cast<uint32_t>(boost));
            end = AppendText(end, ", ");
            end = AppendNumber(end, static_cast<uint32_t>(elapsed.count_microseconds() / 1000U));
            (void)AppendText(end, " ms");
            detail_.SetText(detail_text_);
            app_.log().Info(detail_text_);
            StartPlayback();
        }
        Present();
    }

    void DumpTake() {
        constexpr char kHex[] = "0123456789abcdef";
        constexpr uint32_t kPerLine = 160U;
        char line[2U + kPerLine * 4U + 1U];
        for (uint32_t start = 0U; start < kRecordSamples; start += kPerLine) {
            line[0] = 'S';
            line[1] = ' ';
            for (uint32_t index = 0U; index < kPerLine; ++index) {
                const uint16_t value = static_cast<uint16_t>(recording[start + index]);
                line[2U + index * 4U] = kHex[(value >> 12U) & 15U];
                line[3U + index * 4U] = kHex[(value >> 8U) & 15U];
                line[4U + index * 4U] = kHex[(value >> 4U) & 15U];
                line[5U + index * 4U] = kHex[value & 15U];
            }
            line[sizeof(line) - 1U] = '\0';
            app_.log().Info(line);
        }
    }

    void StartPlayback() {
        micropixel::PcmStreamOptions options{};
        options.sample_rate = kSampleRate;
        options.channels = 1U;
        options.capacity_frames = kStreamCapacity;
        options.low_water_frames = kStreamLowWater;
        auto stream = app_.audio().OpenPcmStream(options);
        if (!stream) {
            ShowStatus("Speaker failed", kRecord);
            mode_ = Mode::kIdle;
            return;
        }
        stream_.emplace(std::move(*stream));
        mode_ = Mode::kPlaying;
        played_ = 0U;
        ShowStatus("Playing back", kPlay);
        Feed();
    }

    void Feed() {
        if (mode_ != Mode::kPlaying || !stream_) {
            return;
        }
        if (played_ < kRecordSamples) {
            const auto accepted = stream_->Write(recording + played_, kRecordSamples - played_);
            if (accepted) {
                played_ += *accepted;
            }
            const uint32_t position = played_ > kStreamCapacity ? played_ - kStreamCapacity : 0U;
            SetBar(static_cast<int32_t>(position * 32767U / kRecordSamples), kPlay);
            Present();
        }
        if (played_ >= kRecordSamples && !drain_) {
            // Let the last ring's worth reach the speaker before closing.
            drain_.emplace(app_.timers().After(Duration::Milliseconds(kStreamCapacity * 1000U / kSampleRate)).value());
        }
    }

    void FinishPlayback() {
        drain_.reset();
        stream_.reset();
        mode_ = Mode::kIdle;
        SetBar(0, kPlay);
        ShowStatus("Ready", kText);
        Present();
    }

    void SetBar(int32_t level, Color color) {
        const int32_t width = static_cast<int32_t>(static_cast<int64_t>(bar_track_.width) * level / 32767);
        bar_.SetRect({bar_track_.x, bar_track_.y, width, bar_track_.height});
        bar_.SetFillColor(color);
    }

    void ShowStatus(const char* text, Color color) {
        status_.SetText(text);
        status_.SetColor(color);
    }

    void Present() { (void)app_.renderer().Present(scene_); }

    micropixel::Application& app_;
    micropixel::Microphone microphone_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::LabelNode status_;
    micropixel::LabelNode detail_;
    micropixel::LabelNode hint_;
    micropixel::RoundedRectNode bar_;
    Rect bar_track_{};
    std::optional<micropixel::PcmStream> stream_;
    std::optional<micropixel::Timer> drain_;
    std::optional<micropixel::Timer> cue_;
    micropixel::TimePoint started_at_{};
    char detail_text_[64]{};
    int32_t width_{};
    int32_t height_{};
    int32_t peak_{};
    uint32_t recorded_{};
    uint32_t played_{};
    uint32_t dropped_{};
    Mode mode_{Mode::kIdle};
};

}  // namespace

int main() {
    micropixel::Application app;
    MicTestApp mic_test(app);
    app.log().Info("Mic Test ready");
    app.Run([&mic_test](const micropixel::Event& event) { return mic_test.Handle(event); });
    return 0;
}
