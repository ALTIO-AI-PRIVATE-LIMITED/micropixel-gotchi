// SPDX-License-Identifier: Apache-2.0
//
// Walkie-Talkie: push-to-talk voice between two paired Gotchis over the Peer
// Radio (ESP-NOW). Half duplex, 16 kHz G.711 mu-law in 10 ms packets, an 80 ms
// jitter buffer on the listening side and a roger beep when the talker lets go.
// The talker high-passes, noise-gates and levels the mic before encoding.
//
// Controls: hold VOL+ (or the TALK circle) to talk; double-tap to ring the
// other Gotchi. VOL- steps the volume. BOOT pairs: press it on both, check
// the 4-digit codes match, press BOOT again on both.
#include <stdint.h>

#include <cstring>
#include <optional>

#include "tone_trace.hpp"
#include "voice.hpp"
#include "sdk/micropixel.hpp"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::KeyCode;
using micropixel::KeyPhase;
using micropixel::Point;
using micropixel::Rect;
using micropixel::SystemFont;

constexpr Color kBackground = Color::Rgb(14U, 16U, 22U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kIdle = Color::Rgb(64U, 120U, 255U);
constexpr Color kTalk = Color::Rgb(240U, 72U, 88U);
constexpr Color kListen = Color::Rgb(72U, 200U, 120U);
constexpr Color kWait = Color::Rgb(90U, 96U, 110U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);

constexpr uint32_t kSampleRate = 16000U;
constexpr uint32_t kFrameSamples = 160U;  // 10 ms
constexpr uint32_t kFrameBytes = kFrameSamples;  // mu-law: one byte per sample
constexpr uint32_t kJitterFrames = 8U;  // 80 ms before playback starts
// Lost frames concealed by fading the last good one; beyond this, silence.
constexpr int16_t kMaxConcealFrames = 3;
constexpr uint32_t kStreamCapacity = 2048U;
// A talker that goes quiet this long without END has gone (out of range).
constexpr uint64_t kListenTimeoutUs = 400000U;
constexpr uint64_t kDoubleTapUs = 350000U;
// Analog mic gain; the board default (30 dB) leaves speech from arm's length
// near -50 dBFS while the noise floor sits near -70 dBFS.
constexpr uint8_t kMicGainDb = 33U;
// Talker-side automatic gain: lift quiet speech toward kAgcTargetPeak, at
// most 2x, backing off at once on loud frames. More gain than this makes two
// Gotchis in the same room howl (listener speaker -> talker mic).
constexpr int32_t kAgcTargetPeak = 12000;
constexpr uint32_t kAgcMaxGainQ8 = 2U * 256U;
// Howl guard: feedback holds the raw mic near full scale without the gaps
// speech has. After kHowlFrames such frames in a row the talker halves its
// level, down to 1/8, and recovers once the mic calms down.
constexpr int32_t kHowlPeak = 20000;
constexpr uint32_t kHowlFrames = 16U;
constexpr uint32_t kHowlMaxShift = 3U;
// Diagnostic: log every received audio packet as hex so the exact audio the
// listener played can be rebuilt off-device.
constexpr bool kDumpPackets = false;

constexpr uint8_t kMagic = 'W';
enum PacketFlag : uint8_t {
    kAudio = 1U << 0U,
    kStart = 1U << 1U,
    kEnd = 1U << 2U,
    kRing = 1U << 3U,
};

struct PacketHeader final {
    uint8_t magic;
    uint8_t flags;
    uint16_t sequence;
};
static_assert(sizeof(PacketHeader) + kFrameBytes <= micropixel::PeerRadio::kMaxPayload);

constexpr uint16_t kVolumeLevels[] = {250U, 500U, 750U, 1000U};

constexpr micropixel::ToneSpec kTalkPermit[] = {
    {micropixel::Waveform::kSine, 1200U, 50U, 250U, 2U, 15U, 0U},
};
constexpr micropixel::ToneSpec kRoger[] = {
    {micropixel::Waveform::kSine, 1400U, 70U, 260U, 2U, 20U, 0U},
    {micropixel::Waveform::kSine, 1000U, 90U, 260U, 2U, 30U, 80U},
};
constexpr micropixel::ToneSpec kRingTones[] = {
    {micropixel::Waveform::kTriangle, 880U, 120U, 420U, 4U, 30U, 0U},
    {micropixel::Waveform::kTriangle, 1175U, 120U, 420U, 4U, 30U, 150U},
    {micropixel::Waveform::kTriangle, 880U, 120U, 420U, 4U, 30U, 450U},
    {micropixel::Waveform::kTriangle, 1175U, 120U, 420U, 4U, 30U, 600U},
};
constexpr micropixel::ToneSpec kBusy[] = {
    {micropixel::Waveform::kSquare, 300U, 120U, 200U, 2U, 30U, 0U},
};

class Text final {
   public:
    Text& Add(const char* text) {
        while (*text != '\0' && length_ + 1U < sizeof(buffer_)) {
            buffer_[length_++] = *text++;
        }
        buffer_[length_] = '\0';
        return *this;
    }
    Text& Number(int32_t value) {
        if (value < 0) {
            Add("-");
            value = -value;
        }
        char digits[12];
        uint32_t count = 0U;
        uint32_t magnitude = static_cast<uint32_t>(value);
        do {
            digits[count++] = static_cast<char>('0' + magnitude % 10U);
            magnitude /= 10U;
        } while (magnitude != 0U);
        char ordered[12];
        for (uint32_t index = 0U; index < count; ++index) {
            ordered[index] = digits[count - 1U - index];
        }
        ordered[count] = '\0';
        return Add(ordered);
    }
    Text& Code(uint16_t code) {
        const char digits[5] = {static_cast<char>('0' + code / 1000U % 10U),
                                static_cast<char>('0' + code / 100U % 10U), static_cast<char>('0' + code / 10U % 10U),
                                static_cast<char>('0' + code % 10U), '\0'};
        return Add(digits);
    }
    [[nodiscard]] const char* c_str() const { return buffer_; }

   private:
    char buffer_[64]{};
    uint32_t length_{};
};

class WalkieTalkieApp final {
   public:
    explicit WalkieTalkieApp(micropixel::Application& app)
        : app_(app),
          radio_(app.peer_radio()),
          microphone_(app.microphone()),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(100U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        const int32_t margin = width_ * 5 / 100;
        title_ = scene_.CreateLabel({margin, height_ * 3 / 100}, "Walkie-Talkie", kText, SystemFont::kMedium).value();
        signal_ = scene_.CreateLabel({width_ * 62 / 100, height_ * 3 / 100}, " ", kMuted, SystemFont::kSmall).value();
        diameter_ = height_ * 52 / 100;
        button_ = {(width_ - diameter_) / 2, height_ * 16 / 100, diameter_, diameter_};
        micropixel::RoundedRectStyle style{};
        style.fill = kWait;
        style.stroke = Color::Mix(Color::White(), kWait, 80U);
        style.stroke_width = 5U;
        style.radius = static_cast<uint32_t>(diameter_);
        circle_ = scene_.CreateRoundedRect(button_, style).value();
        badge_ = scene_
                     .CreateLabel({width_ / 2, button_.y + diameter_ / 2 - height_ * 5 / 100}, " ", Color::White(),
                                  SystemFont::kTitle, true)
                     .value();
        status_ = scene_.CreateLabel({width_ / 2, height_ * 72 / 100}, " ", kText, SystemFont::kLarge, true).value();
        hint_ = scene_.CreateLabel({width_ / 2, height_ * 87 / 100}, " ", kMuted, SystemFont::kSmall, true).value();

        if (!radio_.Start()) {
            fatal_ = "Radio unavailable";
        }
        const auto mic = microphone_.Info();
        if (!mic || !mic->available) {
            fatal_ = "No microphone";
        }
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (const auto* captured = event.microphone()) {
            (void)captured;
            Capture();
        } else if (const auto* peer = event.peer_radio()) {
            if (peer->kind() == micropixel::PeerRadioEventKind::kPacket) {
                Drain();
            } else if (peer->kind() == micropixel::PeerRadioEventKind::kCandidate) {
                std::memcpy(candidate_, peer->address(), sizeof(candidate_));
                candidate_code_ = peer->code();
                has_candidate_ = true;
            }
            Refresh();
        } else if (const auto* tick = event.TimerFrom(tick_)) {
            tones_.Advance(tick->delta());
            CheckListenTimeout();
            Refresh();
        } else if (const auto* key = event.key()) {
            HandleKey(*key);
        } else if (const auto* touch = event.touch()) {
            HandleTouch(*touch);
        } else if (event.type() == micropixel::EventType::kStop) {
            StopTalking(false);
            (void)radio_.Stop();
        } else if (event.type() == micropixel::EventType::kResume) {
            Refresh();
        }
        return micropixel::EventResult::kContinue;
    }

   private:
    [[nodiscard]] uint64_t Now() const { return app_.clock().Now().microseconds(); }
    [[nodiscard]] bool Listening() const { return listen_active_; }

    void HandleKey(const micropixel::KeyEvent& key) {
        if (key.code() == KeyCode::kRight) {
            if (key.phase() == KeyPhase::kDown) {
                Press();
            } else if (key.phase() == KeyPhase::kUp || key.phase() == KeyPhase::kCancel) {
                Release();
            }
        } else if (key.phase() == KeyPhase::kDown && key.code() == KeyCode::kLeft) {
            volume_ = (volume_ + 1U) % (sizeof(kVolumeLevels) / sizeof(kVolumeLevels[0]));
            volume_shown_until_ = Now() + 1500000U;
            (void)tones_.Play(kTalkPermit);
            Refresh();
        } else if (key.phase() == KeyPhase::kDown && key.code() == KeyCode::kConfirm) {
            if (has_candidate_) {
                if (radio_.Accept(candidate_)) {
                    has_candidate_ = false;
                }
            } else {
                (void)radio_.BeginPairing();
            }
            Refresh();
        }
    }

    void HandleTouch(const micropixel::TouchEvent& touch) {
        const Point point = touch.position();
        const bool inside = point.x >= button_.x && point.x < button_.x + button_.width && point.y >= button_.y &&
                            point.y < button_.y + button_.height;
        if (touch.phase() == micropixel::TouchPhase::kDown && inside) {
            touch_talk_ = true;
            Press();
        } else if ((touch.phase() == micropixel::TouchPhase::kUp || touch.phase() == micropixel::TouchPhase::kCancel) &&
                   touch_talk_) {
            touch_talk_ = false;
            Release();
        }
    }

    // Talking needs a pairing, not a strong link: a weak link still carries
    // most packets, and the radio stays on the peer's channel for 15 s.
    [[nodiscard]] bool Linked() const {
        const auto state = radio_.State();
        return state && state->paired;
    }

    void Press() {
        if (talking_ || fatal_ != nullptr) {
            return;
        }
        if (!Linked()) {
            (void)tones_.Play(kBusy);
            return;
        }
        if (Listening()) {
            // Half duplex: the channel is taken.
            (void)tones_.Play(kBusy);
            busy_until_ = Now() + 800000U;
            return;
        }
        const uint64_t now = Now();
        if (last_release_us_ != 0U && now - last_release_us_ < kDoubleTapUs && !spoke_) {
            SendRing();
            last_release_us_ = 0U;
            Refresh();
            return;
        }
        if (!microphone_.Start(kMicGainDb)) {
            fatal_ = "Mic start failed";
            Refresh();
            return;
        }
        talking_ = true;
        spoke_ = false;
        pressed_at_us_ = now;
        pending_ = 0U;
        high_pass_.Reset();
        gate_.Reset();
        agc_gain_q8_ = 256U;
        loud_frames_ = 0U;
        howl_shift_ = 0U;
        sent_trace_.Reset();
        first_packet_ = true;
        (void)tones_.Play(kTalkPermit);
        Refresh();
    }

    void Release() {
        if (!talking_) {
            return;
        }
        // A short tap is a ring candidate, not speech.
        spoke_ = Now() - pressed_at_us_ > 300000U;
        last_release_us_ = Now();
        StopTalking(spoke_);
        Refresh();
    }

    void StopTalking(bool send_end) {
        if (!talking_) {
            return;
        }
        talking_ = false;
        (void)microphone_.Stop();
        app_.log().Info(Text{}.Add("sent ").Number(static_cast<int32_t>(frames_sent_)).Add(" frames").c_str());
        app_.log().Info(sent_trace_.c_str());
        frames_sent_ = 0U;
        if (send_end && !first_packet_) {
            PacketHeader header{kMagic, kEnd, ++sequence_};
            // Three copies: losing the END costs the listener 400 ms of waiting.
            for (uint32_t copy = 0U; copy < 3U; ++copy) {
                (void)radio_.Send(&header, sizeof(header));
            }
        }
    }

    void SendRing() {
        PacketHeader header{kMagic, kRing, ++sequence_};
        for (uint32_t copy = 0U; copy < 3U; ++copy) {
            (void)radio_.Send(&header, sizeof(header));
        }
        (void)tones_.Play(kRingTones);
        rang_until_ = Now() + 1500000U;
    }

    void Capture() {
        if (!talking_) {
            return;
        }
        for (;;) {
            const auto read = microphone_.Read(capture_ + pending_, kFrameSamples - pending_);
            if (!read || *read == 0U) {
                return;
            }
            pending_ += *read;
            if (pending_ < kFrameSamples) {
                continue;
            }
            pending_ = 0U;
            uint8_t packet[sizeof(PacketHeader) + kFrameBytes];
            PacketHeader header{};
            header.magic = kMagic;
            header.flags = static_cast<uint8_t>(kAudio | (first_packet_ ? kStart : 0U));
            header.sequence = ++sequence_;
            std::memcpy(packet, &header, sizeof(header));
            high_pass_.Process(capture_, kFrameSamples);
            gate_.Process(capture_, kFrameSamples);
            ApplyAgc(capture_, kFrameSamples);
            NoteLevel(capture_, kFrameSamples);
            sent_trace_.Add(capture_, kFrameSamples);
            for (uint32_t index = 0U; index < kFrameSamples; ++index) {
                packet[sizeof(header) + index] = walkie::MuLawEncode(capture_[index]);
            }
            if (radio_.Send(packet, sizeof(packet))) {
                ++frames_sent_;
            }
            first_packet_ = false;
        }
    }

    void ApplyAgc(int16_t* samples, uint32_t count) {
        int32_t peak = 1;
        for (uint32_t index = 0U; index < count; ++index) {
            const int32_t magnitude = samples[index] < 0 ? -samples[index] : samples[index];
            peak = magnitude > peak ? magnitude : peak;
        }
        if (peak >= kHowlPeak) {
            if (++loud_frames_ >= kHowlFrames && howl_shift_ < kHowlMaxShift) {
                ++howl_shift_;
                loud_frames_ = 0U;
                app_.log().Warning("howl guard: lowering talk level");
            }
        } else {
            loud_frames_ = 0U;
            if (peak < kHowlPeak / 4 && howl_shift_ > 0U) {
                --howl_shift_;
            }
        }
        if (howl_shift_ != 0U) {
            for (uint32_t index = 0U; index < count; ++index) {
                samples[index] = static_cast<int16_t>(samples[index] >> howl_shift_);
            }
            peak >>= howl_shift_;
            agc_gain_q8_ = 256U;
        }
        const uint32_t wanted = static_cast<uint32_t>(kAgcTargetPeak) * 256U / static_cast<uint32_t>(peak);
        if (wanted < agc_gain_q8_) {
            agc_gain_q8_ = wanted < 256U ? 256U : wanted;  // Back off at once.
        } else if (gate_.open()) {
            // Only speech may raise the gain; pauses hold it.
            agc_gain_q8_ += agc_gain_q8_ / 64U + 1U;  // About +6 dB per 0.5 s.
        }
        agc_gain_q8_ = agc_gain_q8_ > kAgcMaxGainQ8 ? kAgcMaxGainQ8 : agc_gain_q8_;
        for (uint32_t index = 0U; index < count; ++index) {
            int32_t value = static_cast<int32_t>(samples[index]) * static_cast<int32_t>(agc_gain_q8_) / 256;
            value = value > 32767 ? 32767 : (value < -32768 ? -32768 : value);
            samples[index] = static_cast<int16_t>(value);
        }
    }

    // Peak of the frames sent since the last redraw, for the talk meter.
    void NoteLevel(const int16_t* samples, uint32_t count) {
        for (uint32_t index = 0U; index < count; ++index) {
            const int32_t magnitude = samples[index] < 0 ? -samples[index] : samples[index];
            level_peak_ = magnitude > level_peak_ ? magnitude : level_peak_;
        }
    }

    [[nodiscard]] const char* LevelMeter() {
        // 0..10 bars on a rough dB scale: each bar is about 3.5 dB.
        uint32_t bars = 0U;
        for (int32_t threshold = 300; threshold <= level_peak_ && bars < 10U; threshold = threshold * 3 / 2) {
            ++bars;
        }
        level_peak_ = 0;
        meter_ = Text{}.Add("Mic ");
        for (uint32_t bar = 0U; bar < 10U; ++bar) {
            meter_.Add(bar < bars ? "|" : ".");
        }
        if (howl_shift_ != 0U) {
            meter_.Add("  too close?");
        }
        return meter_.c_str();
    }

    void Drain() {
        uint8_t data[micropixel::PeerRadio::kMaxPayload];
        for (;;) {
            const auto packet = radio_.Receive(data);
            if (!packet || packet->length == 0U) {
                return;
            }
            rssi_ = packet->rssi;
            if (packet->length < sizeof(PacketHeader) || data[0] != kMagic) {
                continue;
            }
            PacketHeader header{};
            std::memcpy(&header, data, sizeof(header));
            if ((header.flags & kRing) != 0U) {
                if (header.sequence != last_ring_sequence_) {
                    last_ring_sequence_ = header.sequence;
                    (void)tones_.Play(kRingTones);
                    ringing_until_ = Now() + 2500000U;
                }
            } else if ((header.flags & kEnd) != 0U) {
                EndListening(true);
            } else if ((header.flags & kAudio) != 0U && packet->length == sizeof(header) + kFrameBytes) {
                ReceiveAudio(header, data + sizeof(header));
            }
        }
    }

    void ReceiveAudio(const PacketHeader& header, const uint8_t* payload) {
        if (talking_) {
            return;  // Both pressed at once; keep talking, ignore theirs.
        }
        last_audio_us_ = Now();
        if (!listen_active_) {
            listen_active_ = true;
            heard_trace_.Reset();
            jitter_count_ = 0U;
            playing_ = false;
            expected_sequence_ = header.sequence;
            if (!stream_) {
                micropixel::PcmStreamOptions options{};
                options.sample_rate = kSampleRate;
                options.channels = 1U;
                options.capacity_frames = kStreamCapacity;
                options.low_water_frames = 0U;
                auto stream = app_.audio().OpenPcmStream(options);
                if (stream) {
                    stream_.emplace(std::move(*stream));
                }
            }
        }
        // Late or duplicate frames are useless to a live stream.
        const int16_t ahead = static_cast<int16_t>(header.sequence - expected_sequence_);
        if (ahead < 0) {
            return;
        }
        // Conceal up to two lost frames by fading the last good one.
        for (int16_t missing = 0; missing < ahead && missing < kMaxConcealFrames; ++missing) {
            for (uint32_t index = 0U; index < kFrameSamples; ++index) {
                last_frame_[index] = static_cast<int16_t>(last_frame_[index] / 2);
            }
            Queue(last_frame_);
            ++frames_lost_;
        }
        if (kDumpPackets) {
            constexpr char kHex[] = "0123456789abcdef";
            char line[2U + 2U * (sizeof(PacketHeader) + kFrameBytes) + 1U];
            line[0] = 'P';
            line[1] = ' ';
            uint8_t raw[sizeof(PacketHeader) + kFrameBytes];
            std::memcpy(raw, &header, sizeof(header));
            std::memcpy(raw + sizeof(header), payload, kFrameBytes);
            for (uint32_t index = 0U; index < sizeof(raw); ++index) {
                line[2U + index * 2U] = kHex[raw[index] >> 4U];
                line[3U + index * 2U] = kHex[raw[index] & 15U];
            }
            line[sizeof(line) - 1U] = '\0';
            app_.log().Info(line);
        }
        for (uint32_t index = 0U; index < kFrameSamples; ++index) {
            last_frame_[index] = walkie::MuLawDecode(payload[index]);
        }
        heard_trace_.Add(last_frame_, kFrameSamples);
        const uint32_t gain = kVolumeLevels[volume_];
        for (uint32_t index = 0U; index < kFrameSamples; ++index) {
            last_frame_[index] = static_cast<int16_t>(static_cast<int32_t>(last_frame_[index]) * gain / 1000U);
        }
        Queue(last_frame_);
        expected_sequence_ = static_cast<uint16_t>(header.sequence + 1U);
        ++frames_received_;
    }

    // Holds the first kJitterFrames frames, then streams every frame as it
    // arrives; the PCM ring keeps that 60 ms lead through arrival jitter.
    void Queue(const int16_t* frame) {
        // The dump build stays silent so two units side by side cannot howl.
        if (!stream_ || kDumpPackets) {
            return;
        }
        if (!playing_) {
            std::memcpy(jitter_[jitter_count_], frame, sizeof(jitter_[0]));
            if (++jitter_count_ < kJitterFrames) {
                return;
            }
            playing_ = true;
            for (uint32_t index = 0U; index < jitter_count_; ++index) {
                (void)stream_->Write(jitter_[index], kFrameSamples);
            }
            jitter_count_ = 0U;
            return;
        }
        // A full ring means the talker's clock runs ahead; drop to stay live.
        (void)stream_->Write(frame, kFrameSamples);
    }

    void EndListening(bool roger) {
        if (!listen_active_) {
            return;
        }
        if (!playing_ && stream_) {
            for (uint32_t index = 0U; index < jitter_count_; ++index) {
                (void)stream_->Write(jitter_[index], kFrameSamples);
            }
        }
        listen_active_ = false;
        playing_ = false;
        jitter_count_ = 0U;
        app_.log().Info(Text{}
                            .Add("heard ")
                            .Number(static_cast<int32_t>(frames_received_))
                            .Add(" frames, concealed ")
                            .Number(static_cast<int32_t>(frames_lost_))
                            .Add(roger ? ", roger" : ", timeout")
                            .c_str());
        app_.log().Info(heard_trace_.c_str());
        frames_received_ = 0U;
        frames_lost_ = 0U;
        if (roger) {
            (void)tones_.Play(kRoger);
        }
    }

    void CheckListenTimeout() {
        if (listen_active_ && Now() - last_audio_us_ > kListenTimeoutUs) {
            EndListening(false);
        }
    }

    void SetButton(Color color, const char* badge) {
        circle_.SetFillColor(color);
        circle_.SetStrokeColor(Color::Mix(Color::White(), color, 80U));
        badge_.SetText(badge);
    }

    void Refresh() {
        const uint64_t now = Now();
        const auto state = radio_.State();
        if (fatal_ != nullptr) {
            SetButton(kWait, "!");
            status_.SetText(fatal_);
            status_.SetColor(kTalk);
            hint_.SetText(" ");
        } else if (!state || !state->paired || has_candidate_ || state->pairing) {
            SetButton(kWait, has_candidate_ ? Text{}.Code(candidate_code_).c_str() : "PAIR");
            if (has_candidate_) {
                status_.SetText("Same code on both?");
                hint_.SetText("BOOT to accept on both Gotchis");
            } else if (state && state->pairing) {
                status_.SetText("Pairing...");
                hint_.SetText("Press BOOT on the other Gotchi");
            } else {
                status_.SetText("Not paired");
                hint_.SetText("Press BOOT on both Gotchis");
            }
            status_.SetColor(kWarn);
        } else if (talking_) {
            SetButton(kTalk, "TALK");
            status_.SetText("Talking");
            status_.SetColor(kTalk);
            hint_.SetText(LevelMeter());
        } else if (Listening()) {
            SetButton(kListen, "RX");
            status_.SetText(now < busy_until_ ? "Busy - they're talking" : "Listening");
            status_.SetColor(kListen);
            hint_.SetText(" ");
        } else if (now < ringing_until_) {
            SetButton(kWarn, "RING");
            status_.SetText("Ringing!");
            status_.SetColor(kWarn);
            hint_.SetText("Hold VOL+ to answer");
        } else if (!state->peer_present) {
            SetButton(kIdle, "TALK");
            status_.SetText("No signal");
            status_.SetColor(kWarn);
            hint_.SetText("Out of range? Hold VOL+ to try anyway");
        } else {
            SetButton(kIdle, "TALK");
            status_.SetText(now < rang_until_ ? "Rang them" : "Ready");
            status_.SetColor(kText);
            hint_.SetText(now < volume_shown_until_
                              ? Text{}.Add("Volume ").Number(kVolumeLevels[volume_] / 10U).Add("%").c_str()
                              : "Hold VOL+ to talk, double-tap to ring");
        }
        if (state && state->paired && state->peer_present) {
            signal_.SetText(Text{}.Add("ch").Number(state->channel).Add(" ").Number(rssi_).Add("dBm").c_str());
        } else {
            signal_.SetText(" ");
        }
        (void)app_.renderer().Present(scene_);
    }

    micropixel::Application& app_;
    micropixel::PeerRadio radio_;
    micropixel::Microphone microphone_;
    micropixel::ToneSequencer<4U> tones_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::LabelNode title_;
    micropixel::LabelNode signal_;
    micropixel::LabelNode badge_;
    micropixel::LabelNode status_;
    micropixel::LabelNode hint_;
    micropixel::RoundedRectNode circle_;
    std::optional<micropixel::PcmStream> stream_;
    Rect button_{};
    int32_t width_{};
    int32_t height_{};
    int32_t diameter_{};
    const char* fatal_{};
    // Pairing.
    uint8_t candidate_[6]{};
    uint16_t candidate_code_{};
    bool has_candidate_{};
    // Talking.
    bool talking_{};
    bool touch_talk_{};
    bool spoke_{true};
    bool first_packet_{true};
    uint64_t pressed_at_us_{};
    uint64_t last_release_us_{};
    uint16_t sequence_{};
    uint32_t pending_{};
    int16_t capture_[kFrameSamples]{};
    walkie::HighPass high_pass_;
    walkie::NoiseGate gate_;
    walkie::ToneTrace sent_trace_;
    uint32_t agc_gain_q8_{256U};
    uint32_t loud_frames_{};
    int32_t level_peak_{};
    Text meter_;
    uint32_t howl_shift_{};
    walkie::ToneTrace heard_trace_;
    uint32_t frames_sent_{};
    // Listening.
    bool listen_active_{};
    bool playing_{};
    uint16_t expected_sequence_{};
    uint16_t last_ring_sequence_{};
    uint64_t last_audio_us_{};
    int16_t last_frame_[kFrameSamples]{};
    int16_t jitter_[kJitterFrames][kFrameSamples]{};
    uint32_t jitter_count_{};
    uint32_t frames_received_{};
    uint32_t frames_lost_{};
    int32_t rssi_{};
    uint32_t volume_{2U};
    uint64_t volume_shown_until_{};
    uint64_t busy_until_{};
    uint64_t ringing_until_{};
    uint64_t rang_until_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    WalkieTalkieApp walkie(app);
    app.log().Info("Walkie-Talkie ready");
    app.Run([&walkie](const micropixel::Event& event) { return walkie.Handle(event); });
    return 0;
}
