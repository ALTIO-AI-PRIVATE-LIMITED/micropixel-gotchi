// SPDX-License-Identifier: MIT
//
// Voice Recorder for the Gotchi, after M5VoiceRecorder by mbogatyr
// (https://github.com/mbogatyr/M5VoiceRecorder), MIT. ImaAdpcm.cpp/.h are the
// original codec. The original writes WAV files; the Gotchi has no file store
// for apps, so one 20 second take is held in memory until the app closes.
//
// Controls: tap the big button to record or stop, tap PLAY to hear it back.
#include "apps/port-voice-recorder/ImaAdpcm.h"
#include "apps/portkit/portkit.hpp"

namespace {

namespace math = micropixel::math;
using micropixel::Application;
using micropixel::Duration;
using portkit::Canvas;
using portkit::Rgb;

constexpr uint32_t kSampleRate = 16000U, kBlocks = 640U, kStreamCapacity = 4096U;
constexpr int32_t kBars = 120;

uint8_t store[kBlocks][ima::kBlockBytes];
uint8_t peaks[kBlocks];
int16_t input[ima::kSamplesPerBlock];
int16_t output[ima::kSamplesPerBlock];

enum class Mode { kIdle, kRecording, kPlaying };

class Recorder final {
   public:
    explicit Recorder(Application& app) : app_(app), canvas_(app, Rgb(14, 15, 22), 500U, 8U), mic_(app.microphone()) { Draw(); }

    void Capture() {
        if (mode_ != Mode::kRecording) { return; }
        for (;;) {
            const uint32_t room = ima::kSamplesPerBlock - filled_;
            const auto got = mic_.Read(input + filled_, math::Min(room, micropixel::Microphone::kMaxReadSamples));
            if (!got || *got == 0U) { return; }
            filled_ += *got;
            if (filled_ == ima::kSamplesPerBlock) {
                int32_t peak = 0;
                for (const int16_t sample : input) { peak = math::Max(peak, math::Abs(static_cast<int32_t>(sample))); }
                peaks[blocks_] = static_cast<uint8_t>(math::Min(peak / 64, 255));
                encoder_.encodeBlock(input, store[blocks_]);
                filled_ = 0U;
                if (++blocks_ == kBlocks) { Stop(); return; }
            }
        }
    }

    void Feed() {
        if (mode_ != Mode::kPlaying || !stream_) { return; }
        for (;;) {
            if (offset_ == ima::kSamplesPerBlock) {
                if (played_ == blocks_) { Stop(); return; }
                (void)ima::decodeBlock(store[played_++], output);
                offset_ = 0U;
            }
            const auto accepted = stream_->Write(output + offset_, ima::kSamplesPerBlock - offset_);
            if (!accepted || *accepted == 0U) { return; }
            offset_ += *accepted;
        }
    }

    void Tick() { Draw(); }

    void Touch(const micropixel::TouchEvent& touch) {
        if (touch.phase() != micropixel::TouchPhase::kUp) { return; }
        if (kPlay.Has(touch.x(), touch.y())) { TogglePlay(); } else if (kRecord.Has(touch.x(), touch.y())) { ToggleRecord(); }
    }

    void Key(const micropixel::KeyEvent& key) {
        if (key.phase() != micropixel::KeyPhase::kDown) { return; }
        if (key.code() == micropixel::KeyCode::kConfirm) { ToggleRecord(); } else { TogglePlay(); }
    }

    [[nodiscard]] micropixel::PcmStream* stream() { return stream_ ? &*stream_ : nullptr; }

   private:
    static constexpr portkit::Box kRecord{20, 178, 150, 52}, kPlay{184, 178, 92, 52};

    void ToggleRecord() {
        if (mode_ == Mode::kRecording) { Stop(); return; }
        Stop();
        blocks_ = 0U; filled_ = 0U;
        encoder_.reset();
        if (mic_.Start().has_value()) { mode_ = Mode::kRecording; failed_ = false; } else { failed_ = true; }
    }

    void TogglePlay() {
        if (mode_ == Mode::kPlaying) { Stop(); return; }
        if (mode_ == Mode::kRecording || blocks_ == 0U) { return; }
        micropixel::PcmStreamOptions options{};
        options.sample_rate = kSampleRate;
        options.channels = 1U;
        options.capacity_frames = kStreamCapacity;
        options.low_water_frames = 1024U;
        auto opened = app_.audio().OpenPcmStream(options);
        if (!opened) { failed_ = true; return; }
        stream_.emplace(static_cast<micropixel::PcmStream&&>(*opened));
        played_ = 0U; offset_ = ima::kSamplesPerBlock;
        mode_ = Mode::kPlaying;
        Feed();
    }

    void Stop() {
        if (mode_ == Mode::kRecording) { (void)mic_.Stop(); }
        mode_ = Mode::kIdle;
        stream_.reset();
    }

    void Draw() {
        canvas_.Begin();
        const bool recording = mode_ == Mode::kRecording, playing = mode_ == Mode::kPlaying;
        canvas_.Text(12, 8, failed_ ? "AUDIO NOT AVAILABLE" : (recording ? "RECORDING" : (playing ? "PLAYING" : "VOICE RECORDER")),
                     recording ? Rgb(255, 90, 90) : Rgb(190, 200, 220));
        const uint32_t tenths = blocks_ * ima::kSamplesPerBlock / (kSampleRate / 10U);
        canvas_.Number(12, 30, static_cast<int32_t>(tenths / 10U), Rgb(255, 255, 255), micropixel::SystemFont::kLarge);
        canvas_.Text(70, 40, "of 20 s", Rgb(120, 130, 150));
        const int32_t mid = 124, span = blocks_ == 0U ? 0 : math::Min<int32_t>(kBars, static_cast<int32_t>(blocks_));
        const uint32_t first = blocks_ > static_cast<uint32_t>(kBars) && recording ? blocks_ - kBars : 0U;
        const uint32_t step = (!recording && blocks_ > static_cast<uint32_t>(kBars)) ? blocks_ / kBars : 1U;
        for (int32_t i = 0; i < span; ++i) {
            const int32_t height = math::Max<int32_t>(1, peaks[first + static_cast<uint32_t>(i) * step] / 6);
            const bool past = playing && first + static_cast<uint32_t>(i) * step < played_;
            canvas_.Fill(28 + i * 2, mid - height, 1, height * 2, past ? Rgb(90, 220, 170) : Rgb(100, 140, 230));
        }
        portkit::Button(canvas_, kRecord, recording ? "STOP" : "RECORD", recording ? Rgb(120, 130, 150) : Rgb(226, 54, 62), Rgb(255, 255, 255), micropixel::SystemFont::kMedium);
        portkit::Button(canvas_, kPlay, playing ? "STOP" : "PLAY", blocks_ == 0U ? Rgb(50, 54, 70) : Rgb(60, 180, 130), Rgb(255, 255, 255), micropixel::SystemFont::kMedium);
        canvas_.Present();
    }

    Application& app_;
    Canvas canvas_;
    micropixel::Microphone mic_;
    std::optional<micropixel::PcmStream> stream_{};
    ima::Encoder encoder_{};
    Mode mode_{Mode::kIdle};
    uint32_t blocks_{}, filled_{}, played_{}, offset_{ima::kSamplesPerBlock};
    bool failed_{};
};

}  // namespace

int main() {
    Application app;
    Recorder recorder(app);
    auto timer = app.timers().Every(Duration::Milliseconds(66)).value();
    app.Run([&](const micropixel::Event& event) {
        if (event.microphone() != nullptr) {
            recorder.Capture();
        } else if (auto* stream = recorder.stream(); stream != nullptr && event.LowWaterFrom(*stream) != nullptr) {
            recorder.Feed();
        } else if (event.TimerFrom(timer) != nullptr) {
            recorder.Tick();
        } else if (const auto* touch = event.touch()) {
            recorder.Touch(*touch);
        } else if (const auto* key = event.key()) {
            recorder.Key(*key);
        }
        return micropixel::EventResult::kContinue;
    });
    return 0;
}
