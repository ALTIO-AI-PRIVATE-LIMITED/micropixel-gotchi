// SPDX-License-Identifier: GPL-3.0-or-later
//
// iPhone Agent: ESP32 iPhone Agent (https://github.com/onehumanbeing/esp32-iphone-agent,
// Henry Yuan, MIT) as a Cheeko Gotchi App. The iPhone mirrors its screen to the Gotchi
// over AirPlay; the Gotchi forwards the H.264 stream to the ESP32 Agent iPhone app over
// the Host's BLE Link and acts as the phone's Bluetooth mouse and keyboard (with
// AssistiveTouch on). The phone app holds all the logic; the protocol is the original
// firmware's (docs/ble-protocol.md there), so the phone app needs no changes.
//
// Wi-Fi: Auto mirrors over the Wi-Fi the Gotchi already joined (home, office) and opens
// the Gotchi's own hotspot when no saved network is in range; VOL- / VOL+ switch between
// Auto, Wi-Fi only and Hotspot only.
#include <stdint.h>
#include <stdio.h>

#include <cstring>
#include <new>

#include "airplay/airplay.h"
#include "airplay_host.hpp"
#include "sdk/micropixel.hpp"
#include "video/gop.h"

namespace {

using micropixel::Color;
using micropixel::Duration;
using micropixel::ErrorCode;
using micropixel::SystemFont;

constexpr const char* kReceiverName = "ESP32 Agent";
constexpr uint32_t kTickMs = 15U;      // one HID report per tick, about the BLE connection interval
constexpr int32_t kStep = 10;          // max pointer delta per report while moving
constexpr int32_t kHomeReports = 50;   // reports that slam the pointer into the top-left corner
constexpr uint32_t kCommandMax = 512U;
constexpr uint32_t kQueueDepth = 8U;
constexpr uint64_t kAutoHotspotDelayUs = 10U * 1000000U;  // Auto: no saved Wi-Fi joined by then
constexpr uint8_t kHotspotChannel = 6U;
constexpr uint16_t kAcHome = 0x0223U;  // HID consumer page: AC Home (iOS goes to the Home Screen)

constexpr Color kBackground = Color::Rgb(12U, 14U, 20U);
constexpr Color kText = Color::Rgb(238U, 240U, 244U);
constexpr Color kMuted = Color::Rgb(150U, 158U, 172U);
constexpr Color kGood = Color::Rgb(72U, 200U, 120U);
constexpr Color kWarn = Color::Rgb(250U, 166U, 26U);

enum class WifiMode : uint32_t {
    kAuto = 0U,
    kWifi = 1U,
    kHotspot = 2U,
};

const char* ModeName(WifiMode mode) {
    switch (mode) {
        case WifiMode::kWifi:
            return "Wi-Fi only";
        case WifiMode::kHotspot:
            return "Hotspot only";
        default:
            return "Auto";
    }
}

void FormatAddress(uint32_t address, char (&out)[16]) {
    (void)snprintf(out, sizeof(out), "%u.%u.%u.%u", address & 0xFFU, (address >> 8U) & 0xFFU,
                   (address >> 16U) & 0xFFU, address >> 24U);
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Parses up to two signed decimal integers; returns how many were read.
int ParseInts(const char* text, long& a, long& b) {
    long* outputs[2] = {&a, &b};
    int count = 0;
    while (count < 2) {
        while (IsSpace(*text)) ++text;
        bool negative = false;
        if (*text == '-' || *text == '+') negative = *text++ == '-';
        if (*text < '0' || *text > '9') break;
        long value = 0;
        while (*text >= '0' && *text <= '9') value = value * 10 + (*text++ - '0');
        *outputs[count++] = negative ? -value : value;
    }
    return count;
}

int8_t Clamp8(long value) {
    return static_cast<int8_t>(value < -127 ? -127 : value > 127 ? 127 : value);
}

// US layout: ASCII -> HID Keyboard usage, and whether it needs Shift.
bool AsciiUsage(char c, uint8_t& usage, bool& shift) {
    static const char kUnshifted[] = "-=[]\\;'`,./";
    static const char kShifted[] = "_+{}|:\"~<>?";
    static const uint8_t kPunctuation[] = {0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    static const char kShiftedDigits[] = ")!@#$%^&*(";
    shift = false;
    if (c >= 'a' && c <= 'z') {
        usage = static_cast<uint8_t>(0x04 + (c - 'a'));
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        usage = static_cast<uint8_t>(0x04 + (c - 'A'));
        shift = true;
        return true;
    }
    if (c >= '1' && c <= '9') {
        usage = static_cast<uint8_t>(0x1E + (c - '1'));
        return true;
    }
    switch (c) {
        case '0':
            usage = 0x27;
            return true;
        case '\n':
            usage = 0x28;
            return true;
        case '\t':
            usage = 0x2B;
            return true;
        case ' ':
            usage = 0x2C;
            return true;
        default:
            break;
    }
    for (uint32_t index = 0U; index < sizeof(kPunctuation); ++index) {
        if (c == kUnshifted[index] || c == kShifted[index]) {
            usage = kPunctuation[index];
            shift = c == kShifted[index];
            return true;
        }
    }
    for (uint32_t digit = 0U; digit < 10U; ++digit) {
        if (c == kShiftedDigits[digit]) {
            usage = digit == 0U ? 0x27 : static_cast<uint8_t>(0x1E + digit - 1U);
            shift = true;
            return true;
        }
    }
    return false;
}

// A `key` command part (e.g. "cmd", "enter", "h") -> usage; 0 when unknown.
uint8_t KeyUsage(const char* name, bool& shift) {
    struct Named final {
        const char* name;
        uint8_t usage;
    };
    static const Named kNamed[] = {
        {"enter", 0x28}, {"esc", 0x29},   {"tab", 0x2B},  {"backspace", 0x2A}, {"space", 0x2C},
        {"up", 0x52},    {"down", 0x51},  {"left", 0x50}, {"right", 0x4F},     {"cmd", 0xE3},
        {"ctrl", 0xE0},  {"alt", 0xE2},   {"shift", 0xE1},
    };
    shift = false;
    for (const Named& named : kNamed) {
        if (std::strcmp(name, named.name) == 0) {
            return named.usage;
        }
    }
    uint8_t usage = 0U;
    return name[0] != '\0' && name[1] == '\0' && AsciiUsage(name[0], usage, shift) ? usage : 0U;
}

class AgentApp;
AgentApp* active_agent = nullptr;
// Replies for snap and bench, which finish later (a direct call: the AOT takes no indirect calls).
void VideoReply(const char* id, const char* text);

// The GOP buffer (every access unit since the latest IDR) and the BLE video
// chunker of the original firmware's video_link.cpp, run from the App's event
// loop instead of a task: Pump() sends chunks until the link is busy, and the
// BLE Link room event calls it again.
class VideoLink final {
   public:
    VideoLink(micropixel::BleLink link, micropixel::Clock clock) : link_(link), clock_(clock) {}

    bool Begin() {
        // The original keeps 4 MB of GOP; take what the Guest heap allows.
        for (uint32_t megabytes = 4U; megabytes >= 1U && arena_ == nullptr; --megabytes) {
            capacity_ = megabytes * 1024U * 1024U;
            arena_ = new (std::nothrow) uint8_t[capacity_];
        }
        if (arena_ == nullptr) {
            return false;
        }
        max_aus_ = static_cast<int>(capacity_ / 512U);  // ~27 min of a still screen at 5 fps for 4 MB
        aus_ = new (std::nothrow) gop_au[static_cast<uint32_t>(max_aus_)];
        if (aus_ == nullptr) {
            return false;
        }
        gop_init(&gop_, arena_, capacity_, aus_, max_aus_);
        return true;
    }

    void SetPayload(uint32_t bulk_payload, bool subscribed) {
        payload_ = bulk_payload;
        subscribed_ = subscribed;
    }
    void SetStreaming(bool on) {
        streaming_ = on;
        if (!on) {
            tx_gen_ = kNoGen;
        }
    }

    void OnConfig(const uint8_t* sps_pps, size_t length) {
        gop_set_config(&gop_, sps_pps, length, NowMs());
    }

    void OnAccessUnit(const uint8_t* au, size_t length, bool idr) {
        const uint32_t now = NowMs();
        window_bytes_ += static_cast<uint32_t>(length);
        ++window_frames_;
        if (idr) {
            if (last_idr_ != 0U) idr_ms_ = now - last_idr_;
            last_idr_ = now;
        }
        if (now - window_start_ >= 1000U) {
            kbps_ = window_bytes_ * 8U / (now - window_start_);
            fps10_ = window_frames_ * 10000U / (now - window_start_);
            stats_at_ = now;
            window_bytes_ = 0U;
            window_frames_ = 0U;
            window_start_ = now;
        }
        if (arena_ != nullptr) {
            (void)gop_push(&gop_, au, length, idr, now);
        }
    }

    // Starts a snap (SPS/PPS + the whole GOP); replies when sent. False: reply now with `error`.
    bool Snap(const char* id, const char*& error) {
        if (!subscribed_) {
            error = "err video not subscribed";
            return false;
        }
        if (!HasVideo()) {
            error = "err no video (start screen mirroring)";
            return false;
        }
        CopyId(id);
        activity_ = Activity::kSnap;
        snap_attempts_ = 0U;
        BeginSnap();
        return true;
    }

    bool Bench(const char* id, uint32_t kilobytes, const char*& error) {
        if (!subscribed_) {
            error = "err video not subscribed";
            return false;
        }
        CopyId(id);
        activity_ = Activity::kBench;
        bench_left_ = kilobytes * 1024U;
        bench_start_ = NowMs();
        return true;
    }

    void Pump() {
        for (uint32_t sent = 0U; sent < 24U; ++sent) {
            if (!chunk_pending_ && !NextChunk()) {
                return;
            }
            auto result = link_.Notify(micropixel::BleLinkChannel::kBulk, chunk_, chunk_length_);
            if (result) {
                chunk_pending_ = false;
                continue;
            }
            if (result.error().code() == ErrorCode::kRateLimited) {
                return;  // the room event pumps again
            }
            chunk_pending_ = false;  // its seq is spent: the phone sees a gap and waits for an IDR
            Fail("err ble send failed");
            return;
        }
    }

    [[nodiscard]] uint32_t kbps() const { return Fresh() ? kbps_ : 0U; }
    [[nodiscard]] uint32_t fps10() const { return Fresh() ? fps10_ : 0U; }
    [[nodiscard]] uint32_t idr_ms() const { return idr_ms_; }
    [[nodiscard]] uint32_t gop_bytes() const { return static_cast<uint32_t>(gop_.used); }
    [[nodiscard]] uint32_t capacity() const { return capacity_; }

   private:
    enum class Activity : uint8_t {
        kNone,
        kSnap,
        kBench,
    };
    static constexpr uint32_t kNoGen = UINT32_MAX;

    [[nodiscard]] uint32_t NowMs() const { return static_cast<uint32_t>(clock_.Now().microseconds() / 1000U); }
    [[nodiscard]] bool Fresh() const { return NowMs() - stats_at_ < 3000U; }
    [[nodiscard]] bool HasVideo() const { return arena_ != nullptr && !gop_.wait_idr && gop_.n > 0 && gop_.cfg_len > 0; }
    [[nodiscard]] uint32_t Room() const {
        const uint32_t room = payload_ > VCHUNK_HDR ? payload_ - VCHUNK_HDR : 0U;
        return room < sizeof(chunk_) - VCHUNK_HDR ? room : static_cast<uint32_t>(sizeof(chunk_) - VCHUNK_HDR);
    }

    void CopyId(const char* id) {
        std::strncpy(id_, id, sizeof(id_) - 1U);
        id_[sizeof(id_) - 1U] = '\0';
    }

    void Finish(const char* text) {
        activity_ = Activity::kNone;
        au_active_ = false;
        VideoReply(id_, text);
    }

    void Fail(const char* text) {
        au_active_ = false;
        tx_gen_ = kNoGen;  // stream: start over from the GOP's IDR
        if (activity_ != Activity::kNone) {
            Finish(text);
        }
    }

    void BeginSnap() {
        snap_gen_ = gop_.gen;
        snap_count_ = gop_.n;
        snap_next_ = 0;
        std::memcpy(config_, gop_.cfg, gop_.cfg_len);
        vchunk_begin(&chunk_state_, config_, gop_.cfg_len, VF_CONFIG | VF_SNAP, gop_.cfg_ts);
        au_gen_ = kNoGen;
        au_active_ = true;
    }

    // Builds the next notify into chunk_; false when there is nothing to send.
    bool NextChunk() {
        const uint32_t room = Room();
        if (!subscribed_ || room < 5U) {
            return false;
        }
        for (;;) {
            if (au_active_) {
                if (au_gen_ != kNoGen && au_gen_ != gop_.gen) {
                    // A new IDR replaced the GOP mid-AU: its bytes are gone.
                    au_active_ = false;
                    if (activity_ == Activity::kSnap) {
                        if (++snap_attempts_ >= 3U || !HasVideo()) {
                            Finish("err video changing too fast");
                            return false;
                        }
                        BeginSnap();
                    } else {
                        tx_gen_ = kNoGen;
                    }
                    continue;
                }
                const size_t length = vchunk_next(&chunk_state_, chunk_, room, seq_);
                if (length != 0U) {
                    ++seq_;
                    chunk_length_ = static_cast<uint32_t>(length);
                    chunk_pending_ = true;
                    return true;
                }
                au_active_ = false;
            }
            if (activity_ == Activity::kSnap) {
                if (gop_.gen != snap_gen_) {  // a new IDR between AUs: start over with it
                    if (++snap_attempts_ >= 3U || !HasVideo()) {
                        Finish("err video changing too fast");
                        return false;
                    }
                    BeginSnap();
                    continue;
                }
                if (snap_next_ >= snap_count_) {
                    Finish("ok");
                    continue;
                }
                const gop_au& au = gop_.aus[snap_next_++];
                vchunk_begin(&chunk_state_, gop_.arena + au.off, au.len,
                             static_cast<uint8_t>(VF_SNAP | (au.idr ? VF_IDR : 0)), au.ts);
                au_gen_ = snap_gen_;
                au_active_ = true;
                continue;
            }
            if (activity_ == Activity::kBench) {
                if (bench_left_ == 0U) {
                    char text[24];
                    (void)snprintf(text, sizeof(text), "ok %u", static_cast<unsigned>(NowMs() - bench_start_));
                    Finish(text);
                    continue;
                }
                const uint32_t length = bench_left_ < room ? bench_left_ : room;
                chunk_[0] = static_cast<uint8_t>(seq_);
                chunk_[1] = static_cast<uint8_t>(seq_ >> 8U);
                chunk_[2] = VF_BENCH;
                std::memset(chunk_ + VCHUNK_HDR, 0xA5, length);
                ++seq_;
                bench_left_ -= length;
                chunk_length_ = VCHUNK_HDR + length;
                chunk_pending_ = true;
                return true;
            }
            // Continuous mode: every AU once, catching up from the GOP's IDR when
            // streaming starts (P-frames can't be skipped).
            if (!streaming_ || !HasVideo()) {
                return false;
            }
            if (gop_.gen != tx_gen_) {
                tx_gen_ = gop_.gen;
                tx_next_ = 0;
                std::memcpy(config_, gop_.cfg, gop_.cfg_len);
                vchunk_begin(&chunk_state_, config_, gop_.cfg_len, VF_CONFIG, gop_.cfg_ts);
                au_gen_ = kNoGen;
                au_active_ = true;
                continue;
            }
            if (tx_next_ >= gop_.n) {
                return false;
            }
            const gop_au& au = gop_.aus[tx_next_++];
            vchunk_begin(&chunk_state_, gop_.arena + au.off, au.len, au.idr ? VF_IDR : 0, au.ts);
            au_gen_ = tx_gen_;
            au_active_ = true;
        }
    }

    micropixel::BleLink link_;
    micropixel::Clock clock_;
    gop_buf gop_{};
    uint8_t* arena_{};
    gop_au* aus_{};
    uint32_t capacity_{};
    int max_aus_{};
    uint8_t config_[GOP_CFG_MAX]{};
    uint8_t chunk_[VCHUNK_HDR + micropixel::BleLink::kMaxMessage]{};
    uint32_t chunk_length_{};
    bool chunk_pending_{};
    vchunk chunk_state_{};
    bool au_active_{};
    uint32_t au_gen_{kNoGen};
    uint16_t seq_{};
    uint32_t payload_{20U};
    bool subscribed_{};
    bool streaming_{};
    uint32_t tx_gen_{kNoGen};
    int tx_next_{};
    Activity activity_{Activity::kNone};
    char id_[16]{};
    uint32_t snap_gen_{};
    int snap_count_{};
    int snap_next_{};
    uint32_t snap_attempts_{};
    uint32_t bench_left_{};
    uint32_t bench_start_{};
    uint32_t window_start_{};
    uint32_t window_bytes_{};
    uint32_t window_frames_{};
    uint32_t last_idr_{};
    uint32_t kbps_{};
    uint32_t fps10_{};
    uint32_t idr_ms_{};
    uint32_t stats_at_{};
};

class AgentApp final {
   public:
    explicit AgentApp(micropixel::Application& app)
        : app_(app),
          hid_(app.hid_keyboard()),
          link_(app.ble_link()),
          network_(app.network()),
          storage_(app.storage()),
          video_(app.ble_link(), app.clock()),
          airplay_(app),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          tick_(app.timers().Every(Duration::Milliseconds(kTickMs)).value()),
          second_(app.timers().Every(Duration::Milliseconds(1000U)).value()) {
        active_agent = this;
        BuildUi();
        LoadSettings();
        mode_since_us_ = NowUs();
        (void)hid_.Start();
        if (const auto started = link_.Start(); !started) {
            app_.log().Warning("BLE Link unavailable (needs the iPhone Agent host firmware)");
        }
        if (!video_.Begin()) {
            app_.log().Error("no memory for the GOP buffer");
        }
        RefreshLink();
        UpdateWifi();
        StartAirPlay();
        Refresh();
    }

    micropixel::EventResult Handle(const micropixel::Event& event) {
        if (event.TimerFrom(tick_) != nullptr) {
            RunJob();
            video_.Pump();
            airplay_.Tick();
        } else if (event.TimerFrom(second_) != nullptr) {
            RefreshLink();
            UpdateWifi();
            StartAirPlay();
            SendStatus();
            Refresh();
        } else if (const auto* link = event.ble_link()) {
            if (link->kind() == micropixel::BleLinkEventKind::kInbox) {
                DrainInbox();
            } else if (link->kind() == micropixel::BleLinkEventKind::kState) {
                RefreshLink();
                Refresh();
            } else {
                video_.Pump();
            }
        } else if (const auto* network = event.network()) {
            if (network->kind() == micropixel::NetworkEventKind::kReady) {
                airplay_.OnSocketReady(network->socket());
                video_.Pump();
            } else {
                UpdateWifi();
                Refresh();
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == micropixel::KeyPhase::kDown) {
                if (key->code() == micropixel::KeyCode::kRight) {
                    SetMode(static_cast<WifiMode>((static_cast<uint32_t>(mode_) + 1U) % 3U));
                } else if (key->code() == micropixel::KeyCode::kLeft) {
                    SetMode(static_cast<WifiMode>((static_cast<uint32_t>(mode_) + 2U) % 3U));
                }
            }
        }
        return micropixel::EventResult::kContinue;
    }

    void OnVideoConfig(const uint8_t* data, size_t length) { video_.OnConfig(data, length); }
    void OnVideoAccessUnit(const uint8_t* data, size_t length, bool idr) { video_.OnAccessUnit(data, length, idr); }
    void Log(const char* message) const { app_.log().Info(message); }
    void ReplyFromVideo(const char* id, const char* text) { Reply(id, text); }

   private:
    enum class JobKind : uint8_t {
        kNone,
        kMove,
        kHome,
        kClick,
        kType,
        kCombo,
        kHomeScreen,
        kUnpair,
    };

    struct Command final {
        char id[16]{};
        char text[kCommandMax + 1U]{};
    };

    struct Job final {
        JobKind kind{JobKind::kNone};
        char id[16]{};
        long dx{};
        long dy{};
        int32_t count{};
        uint8_t stage{};
        char text[kCommandMax + 1U]{};
        uint32_t position{};
        uint8_t keys[6]{};
        uint32_t key_count{};
    };

    [[nodiscard]] uint64_t NowUs() const { return app_.clock().Now().microseconds(); }

    // ---- settings ----

    void LoadSettings() {
        mode_ = static_cast<WifiMode>(storage_.GetU32Or("mode", 0U) % 3U);
        width_ = static_cast<uint16_t>(storage_.GetU32Or("w", 480U));
        height_ = static_cast<uint16_t>(storage_.GetU32Or("h", 1040U));
        fps_ = static_cast<uint8_t>(storage_.GetU32Or("fps", 5U));
        const auto state = network_.State();
        if (state) {
            std::memcpy(mac_, state->mac, sizeof(mac_));
        }
        // Hotspot credentials and the AirPlay identity are made once and kept.
        auto ssid = storage_.GetBytes("ssid", reinterpret_cast<uint8_t*>(ssid_), sizeof(ssid_) - 1U);
        if (!ssid || *ssid == 0U) {
            (void)snprintf(ssid_, sizeof(ssid_), "ESP32Agent-%02X%02X", mac_[4], mac_[5]);
            (void)storage_.SetBytes("ssid", reinterpret_cast<const uint8_t*>(ssid_), std::strlen(ssid_));
        }
        auto password = storage_.GetBytes("pass", reinterpret_cast<uint8_t*>(password_), sizeof(password_) - 1U);
        if (!password || *password < 8U) {
            static const char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
            for (uint32_t index = 0U; index < 12U; ++index) {
                password_[index] = kAlphabet[app_.random().Below(sizeof(kAlphabet) - 1U)];
            }
            password_[12] = '\0';
            (void)storage_.SetBytes("pass", reinterpret_cast<const uint8_t*>(password_), 12U);
        }
        auto seed = storage_.GetBytes("seed", seed_, sizeof(seed_));
        if (!seed || *seed != sizeof(seed_)) {
            for (uint32_t index = 0U; index < sizeof(seed_); index += 4U) {
                const uint32_t value = app_.random().U32();
                std::memcpy(seed_ + index, &value, 4U);
            }
            (void)storage_.SetBytes("seed", seed_, sizeof(seed_));
        }
    }

    void SetMode(WifiMode mode) {
        mode_ = mode;
        mode_since_us_ = NowUs();
        (void)storage_.SetU32("mode", static_cast<uint32_t>(mode_));
        UpdateWifi();
        Refresh();
    }

    // ---- Wi-Fi ----

    void UpdateWifi() {
        auto state = network_.State();
        if (!state) {
            return;
        }
        net_ = *state;
        const bool hotspot = net_.access_point;
        if (mode_ == WifiMode::kHotspot && !hotspot) {
            StartHotspot();
        } else if (mode_ == WifiMode::kWifi && hotspot) {
            (void)network_.StopAccessPoint();
        } else if (mode_ == WifiMode::kAuto && !hotspot && !net_.station_connected &&
                   NowUs() - mode_since_us_ > kAutoHotspotDelayUs) {
            // While the hotspot runs the Host makes no station attempts, so Auto stays
            // on it until the mode is changed (or the App restarts somewhere known).
            StartHotspot();
        } else if (mode_ == WifiMode::kAuto && hotspot && net_.station_connected) {
            (void)network_.StopAccessPoint();
        }
        if (auto refreshed = network_.State()) {
            net_ = *refreshed;
        }
        const uint8_t link = net_.access_point ? 2U : net_.station_connected ? 1U : 0U;
        if (link != active_link_) {
            active_link_ = link;
            airplay_.Readvertise();
        }
        PublishNetValue();
    }

    void StartHotspot() {
        if (const auto started = network_.StartAccessPoint(ssid_, password_, kHotspotChannel, 4U); !started) {
            app_.log().Warning("hotspot failed to start");
        }
    }

    void PublishNetValue() {
        char address[16];
        char value[160];
        if (net_.access_point) {
            FormatAddress(net_.access_point_address, address);
            (void)snprintf(value, sizeof(value), "{\"mode\":\"ap\",\"ssid\":\"%s\",\"pass\":\"%s\",\"ip\":\"%s\"}",
                           ssid_, password_, address);
        } else {
            FormatAddress(net_.station_address, address);
            (void)snprintf(value, sizeof(value), "{\"mode\":\"sta\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
                           net_.station_ssid, net_.station_connected ? address : "");
        }
        if (std::strcmp(value, net_value_) != 0) {
            std::strncpy(net_value_, value, sizeof(net_value_) - 1U);
            (void)link_.SetValue(net_value_, static_cast<uint32_t>(std::strlen(net_value_)));
        }
    }

    // The phone app writes {"mode":"ap"} or {"mode":"sta",...} to the net characteristic.
    void OnNetRequest(const char* json) {
        if (std::strstr(json, "\"ap\"") != nullptr) {
            SetMode(WifiMode::kHotspot);
        } else if (std::strstr(json, "\"sta\"") != nullptr) {
            SetMode(WifiMode::kWifi);
        }
    }

    // ---- AirPlay ----

    void StartAirPlay() {
        if (airplay_.started() || (!net_.access_point && !net_.station_connected)) {
            return;
        }
        airplay_cfg config{};
        config.name = kReceiverName;
        std::memcpy(config.hwaddr, mac_, sizeof(config.hwaddr));
        std::memcpy(config.ed25519_seed, seed_, sizeof(config.ed25519_seed));
        config.width = width_;
        config.height = height_;
        config.max_fps = fps_;
        if (airplay_.Start(config)) {
            active_link_ = net_.access_point ? 2U : 1U;
        }
    }

    // ---- BLE link ----

    void RefreshLink() {
        auto state = link_.State();
        auto keyboard = hid_.State();
        const bool was_connected = link_state_.connected;
        if (state) {
            link_state_ = *state;
        }
        mouse_ready_ = keyboard && keyboard->connected && link_state_.encrypted;
        video_.SetPayload(link_state_.bulk_payload, link_state_.bulk_subscribed);
        if (was_connected && !link_state_.connected) {
            // A gesture cut off mid-way must not leave the button held (long press).
            ReleaseAll();
            video_.SetStreaming(false);
            queue_count_ = 0U;
            job_ = {};
        }
    }

    void DrainInbox() {
        static uint8_t data[micropixel::BleLink::kMaxMessage + 1U];
        for (uint32_t round = 0U; round < 16U; ++round) {
            auto message = link_.Read(*reinterpret_cast<uint8_t(*)[micropixel::BleLink::kMaxMessage]>(data));
            if (!message || message->length == 0U) {
                break;
            }
            data[message->length] = '\0';
            if (message->value) {
                OnNetRequest(reinterpret_cast<const char*>(data));
            } else {
                OnCommandLine(reinterpret_cast<char*>(data));
            }
        }
        StartNext();
    }

    void OnCommandLine(char* line) {
        while (IsSpace(*line)) ++line;
        size_t length = std::strlen(line);
        while (length != 0U && IsSpace(line[length - 1U])) line[--length] = '\0';
        char* space = std::strchr(line, ' ');
        char* rest = space != nullptr ? space + 1 : line + length;
        if (space != nullptr) *space = '\0';
        while (IsSpace(*rest)) ++rest;
        bool numeric = line[0] != '\0';
        for (const char* c = line; *c != '\0'; ++c) numeric = numeric && *c >= '0' && *c <= '9';
        if (!numeric || std::strlen(line) >= sizeof(Command::id)) {
            Reply(line, "err missing request id");
            return;
        }
        if (queue_count_ == kQueueDepth) {
            Reply(line, "err busy");
            return;
        }
        Command& command = queue_[(queue_head_ + queue_count_++) % kQueueDepth];
        std::strncpy(command.id, line, sizeof(command.id) - 1U);
        command.id[sizeof(command.id) - 1U] = '\0';
        std::strncpy(command.text, rest, kCommandMax);
        command.text[kCommandMax] = '\0';
    }

    void Reply(const char* id, const char* text) {
        char line[kCommandMax + 24U];
        const int length = snprintf(line, sizeof(line), "%s %s", id, text);
        if (length > 0) {
            const uint32_t size = length < static_cast<int>(sizeof(line)) ? static_cast<uint32_t>(length)
                                                                          : static_cast<uint32_t>(sizeof(line) - 1U);
            (void)link_.Notify(micropixel::BleLinkChannel::kReply, line, size);
        }
    }


    // ---- commands ----

    void StartNext() {
        while (job_.kind == JobKind::kNone && queue_count_ != 0U) {
            Command& command = queue_[queue_head_];
            queue_head_ = (queue_head_ + 1U) % kQueueDepth;
            --queue_count_;
            const char* reply = Execute(command);
            if (reply != nullptr) {
                Reply(command.id, reply);
            }
        }
    }

    // Board-level commands, then the HID commands. Returns the reply, or null
    // when a job (or the video link) replies later.
    const char* Execute(Command& command) {
        char* text = command.text;
        char* space = std::strchr(text, ' ');
        const char* argument = space != nullptr ? space + 1 : "";
        if (space != nullptr) *space = '\0';
        long a = 0;
        long b = 0;
        const int numbers = ParseInts(argument, a, b);
        static char reply[kCommandMax + 8U];

        if (std::strcmp(text, "ping") == 0) return "ok";
        if (std::strcmp(text, "stream") == 0) {
            if (std::strcmp(argument, "on") != 0 && std::strcmp(argument, "off") != 0) return "err usage: stream on|off";
            video_.SetStreaming(std::strcmp(argument, "on") == 0);
            video_.Pump();
            return "ok";
        }
        if (std::strcmp(text, "app") == 0 || std::strcmp(text, "log") == 0) {
            // debug relay between the Mac harness and the phone app; each side ignores what it doesn't handle
            (void)snprintf(reply, sizeof(reply), "%s %s", text, argument);
            (void)link_.Notify(micropixel::BleLinkChannel::kReply, reply, static_cast<uint32_t>(std::strlen(reply)));
            return "ok";
        }
        if (std::strcmp(text, "snap") == 0) {
            const char* error = nullptr;
            if (!video_.Snap(command.id, error)) return error;
            video_.Pump();
            return nullptr;
        }
        if (std::strcmp(text, "bench") == 0) {
            if (numbers < 1 || a <= 0 || a > 4096) return "err usage: bench KB (1-4096)";
            const char* error = nullptr;
            if (!video_.Bench(command.id, static_cast<uint32_t>(a), error)) return error;
            video_.Pump();
            return nullptr;
        }
        if (std::strcmp(text, "res") == 0) {
            if (numbers < 2 || a < 160 || b < 160 || a > 1920 || b > 2560) return "err usage: res W H";
            width_ = static_cast<uint16_t>(a);
            height_ = static_cast<uint16_t>(b);
            (void)storage_.SetU32("w", width_);
            (void)storage_.SetU32("h", height_);
            airplay_set_display(width_, height_, fps_);
            return "ok";
        }
        if (std::strcmp(text, "fps") == 0) {
            if (numbers < 1 || a < 1 || a > 60) return "err usage: fps N (1-60)";
            fps_ = static_cast<uint8_t>(a);
            (void)storage_.SetU32("fps", fps_);
            airplay_set_display(width_, height_, fps_);
            return "ok";
        }
        if (std::strcmp(text, "coex") == 0) return "ok balance";  // the Host owns the radio share
        if (std::strcmp(text, "unpair") == 0) {
            BeginJob(command, JobKind::kUnpair);
            job_.count = 20;  // let the `ok` go out before the link drops
            Reply(command.id, "ok");
            job_.id[0] = '\0';
            return nullptr;
        }
        if (!mouse_ready_) return "err mouse not connected (pair CGotchi in Bluetooth settings)";
        if (std::strcmp(text, "home") == 0) {
            BeginJob(command, JobKind::kHome);
            job_.count = kHomeReports;
            return nullptr;
        }
        if (std::strcmp(text, "move") == 0) {
            BeginJob(command, JobKind::kMove);
            job_.dx = a;
            job_.dy = b;
            return nullptr;
        }
        if (std::strcmp(text, "press") == 0) return Mouse(buttons_ | 1U, 0, 0, 0, 0) ? "ok" : "err mouse report failed";
        if (std::strcmp(text, "release") == 0) {
            return Mouse(buttons_ & ~1U, 0, 0, 0, 0) ? "ok" : "err mouse report failed";
        }
        if (std::strcmp(text, "click") == 0) {
            BeginJob(command, JobKind::kClick);
            return nullptr;
        }
        if (std::strcmp(text, "scroll") == 0) {
            return Mouse(buttons_, 0, 0, Clamp8(a), Clamp8(b)) ? "ok" : "err mouse report failed";
        }
        if (std::strcmp(text, "type") == 0) {
            BeginJob(command, JobKind::kType);
            std::strncpy(job_.text, argument, kCommandMax);
            job_.text[kCommandMax] = '\0';
            return nullptr;
        }
        if (std::strcmp(text, "key") == 0) {
            BeginJob(command, JobKind::kCombo);
            char combo[kCommandMax + 2U];
            std::strncpy(combo, argument, kCommandMax);
            combo[kCommandMax] = '\0';
            bool shift = false;
            for (char* part = combo; *part != '\0';) {
                char* plus = std::strchr(part, '+');
                char* next = plus != nullptr ? plus + 1 : part + std::strlen(part);
                if (plus != nullptr) *plus = '\0';
                const uint8_t usage = KeyUsage(part, shift);
                part = next;
                if (usage == 0U || job_.key_count + (shift ? 2U : 1U) > sizeof(job_.keys)) {
                    job_ = {};
                    return "err unknown key";
                }
                if (shift) job_.keys[job_.key_count++] = 0xE1;
                job_.keys[job_.key_count++] = usage;
            }
            if (job_.key_count == 0U) {
                job_ = {};
                return "err unknown key";
            }
            return nullptr;
        }
        if (std::strcmp(text, "homescreen") == 0) {
            BeginJob(command, JobKind::kHomeScreen);
            return nullptr;
        }
        return "err unknown command";
    }

    void BeginJob(const Command& command, JobKind kind) {
        job_ = {};
        job_.kind = kind;
        std::strncpy(job_.id, command.id, sizeof(job_.id) - 1U);
    }

    void EndJob(const char* reply) {
        if (job_.id[0] != '\0') {
            Reply(job_.id, reply);
        }
        job_ = {};
        StartNext();
    }

    bool Mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
        if (!hid_.Mouse(buttons, dx, dy, wheel, pan)) {
            return false;
        }
        buttons_ = buttons;
        return true;
    }

    bool Key(uint8_t usage, bool pressed) {
        return static_cast<bool>(hid_.Key(static_cast<micropixel::HidKeyUsage>(usage), pressed));
    }

    // One step of the running job per tick: the original firmware's paced HID
    // sequences, one report per BLE connection interval.
    void RunJob() {
        switch (job_.kind) {
            case JobKind::kNone:
                return;
            case JobKind::kMove: {
                if (job_.dx == 0 && job_.dy == 0) {
                    EndJob("ok");
                    return;
                }
                const long sx = job_.dx < -kStep ? -kStep : job_.dx > kStep ? kStep : job_.dx;
                const long sy = job_.dy < -kStep ? -kStep : job_.dy > kStep ? kStep : job_.dy;
                if (!Mouse(buttons_, static_cast<int8_t>(sx), static_cast<int8_t>(sy), 0, 0)) {
                    EndJob("err mouse report failed");
                    return;
                }
                job_.dx -= sx;
                job_.dy -= sy;
                return;
            }
            case JobKind::kHome:
                if (job_.count-- <= 0) {
                    EndJob("ok");
                } else if (!Mouse(buttons_, -127, -127, 0, 0)) {
                    EndJob("err mouse report failed");
                }
                return;
            case JobKind::kClick:
                if (job_.stage == 0U) {
                    if (!Mouse(buttons_ | 1U, 0, 0, 0, 0)) {
                        EndJob("err mouse report failed");
                        return;
                    }
                    job_.stage = 1U;
                    job_.count = 4;  // ~60 ms held
                } else if (--job_.count <= 0) {
                    EndJob(Mouse(buttons_ & ~1U, 0, 0, 0, 0) ? "ok" : "err mouse report failed");
                }
                return;
            case JobKind::kType: {
                // Key down on one tick, up on the next.
                const char c = job_.text[job_.position];
                if (c == '\0') {
                    EndJob("ok");
                    return;
                }
                uint8_t usage = 0U;
                bool shift = false;
                if (!AsciiUsage(c, usage, shift)) {
                    ++job_.position;  // non-ASCII: skipped, as the USB keyboard did
                    return;
                }
                if (job_.stage == 0U) {
                    if (shift) (void)Key(0xE1, true);
                    if (!Key(usage, true)) {
                        EndJob("err keyboard report failed");
                        return;
                    }
                    job_.stage = 1U;
                } else {
                    (void)Key(usage, false);
                    if (shift) (void)Key(0xE1, false);
                    job_.stage = 0U;
                    ++job_.position;
                }
                return;
            }
            case JobKind::kCombo:
                if (job_.stage == 0U) {
                    for (uint32_t index = 0U; index < job_.key_count; ++index) (void)Key(job_.keys[index], true);
                    job_.stage = 1U;
                    job_.count = 2;  // ~30 ms held
                } else if (--job_.count <= 0) {
                    for (uint32_t index = job_.key_count; index > 0U; --index) (void)Key(job_.keys[index - 1U], false);
                    EndJob("ok");
                }
                return;
            case JobKind::kHomeScreen:
                if (job_.stage == 0U) {
                    if (!hid_.Consumer(static_cast<micropixel::HidConsumerUsage>(kAcHome), true)) {
                        EndJob("err keyboard report failed");
                        return;
                    }
                    job_.stage = 1U;
                    job_.count = 3;  // ~40 ms held
                } else if (--job_.count <= 0) {
                    (void)hid_.Consumer(static_cast<micropixel::HidConsumerUsage>(kAcHome), false);
                    EndJob("ok");
                }
                return;
            case JobKind::kUnpair:
                if (--job_.count <= 0) {
                    (void)hid_.ForgetBonds();
                    EndJob("ok");
                }
                return;
        }
    }

    void ReleaseAll() {
        if (buttons_ != 0U) (void)Mouse(0U, 0, 0, 0, 0);
        if (job_.kind == JobKind::kCombo && job_.stage == 1U) {
            for (uint32_t index = 0U; index < job_.key_count; ++index) (void)Key(job_.keys[index], false);
        }
    }

    // ---- status ----

    void SendStatus() {
        static const char* const kAirPlay[] = {"idle", "connected", "streaming"};
        const int airplay = airplay_.State();
        const char* net = net_.access_point ? "ap" : net_.station_connected ? "sta" : "none";
        char address[16];
        FormatAddress(net_.access_point ? net_.access_point_address : net_.station_address, address);
        const int peers = net_.access_point ? net_.access_point_stations : net_.station_connected ? 1 : 0;
        const uint32_t up = static_cast<uint32_t>(NowUs() / 1000U);
        const char* usb = mouse_ready_ ? "ok" : "off";
        const uint32_t fps10 = video_.fps10();
        char status[384];
        int length = snprintf(
            status, sizeof(status),
            "{\"up\":%u,\"rst\":\"power-on\",\"heap\":0,\"psram\":%u,\"usb\":\"%s\",\"net\":\"%s\",\"ip\":\"%s\","
            "\"sta\":%d,\"ap\":\"%s\",\"w\":%u,\"h\":%u,\"kbps\":%u,\"fps\":%u.%u,\"idr\":%u,\"gop\":%u,"
            "\"ble\":{\"mtu\":%u,\"phy\":%u,\"kbps\":%u,\"drop\":%u,\"rdrop\":%u,\"n\":%u}}",
            static_cast<unsigned>(up), static_cast<unsigned>(video_.capacity()), usb, net, address, peers,
            kAirPlay[airplay < 0 || airplay > 2 ? 0 : airplay], width_, height_, static_cast<unsigned>(video_.kbps()),
            static_cast<unsigned>(fps10 / 10U), static_cast<unsigned>(fps10 % 10U),
            static_cast<unsigned>(video_.idr_ms()), static_cast<unsigned>(video_.gop_bytes()), link_state_.mtu,
            link_state_.phy, static_cast<unsigned>(link_state_.bulk_kbps), static_cast<unsigned>(link_state_.bulk_drops),
            static_cast<unsigned>(link_state_.reply_drops), link_state_.peers);
        if (length < 0 || length + 3 > static_cast<int>(link_state_.mtu)) {
            // Before the MTU exchange: only what the phone app needs to stay alive.
            length = snprintf(status, sizeof(status),
                              "{\"up\":%u,\"rst\":\"power-on\",\"usb\":\"%s\",\"net\":\"%s\",\"ap\":\"%s\","
                              "\"ble\":{\"mtu\":%u}}",
                              static_cast<unsigned>(up), usb, net, kAirPlay[airplay < 0 || airplay > 2 ? 0 : airplay],
                              link_state_.mtu);
        }
        if (length > 0 && link_state_.connected) {
            (void)link_.Notify(micropixel::BleLinkChannel::kStatus, status, static_cast<uint32_t>(length));
        }
    }

    // ---- screen ----

    void BuildUi() {
        const int32_t width = static_cast<int32_t>(info_.width());
        const int32_t height = static_cast<int32_t>(info_.height());
        const int32_t margin = width * 5 / 100;
        (void)scene_.CreateLabel({margin, height * 3 / 100}, "iPhone Agent", kText, SystemFont::kMedium).value();
        iphone_ = scene_.CreateLabel({margin, height * 17 / 100}, "", kText, SystemFont::kSmall).value();
        mouse_ = scene_.CreateLabel({margin, height * 28 / 100}, "", kText, SystemFont::kSmall).value();
        wifi_ = scene_.CreateLabel({margin, height * 42 / 100}, "", kText, SystemFont::kSmall).value();
        wifi_detail_ = scene_.CreateLabel({margin, height * 53 / 100}, "", kMuted, SystemFont::kSmall).value();
        wifi_secret_ = scene_.CreateLabel({margin, height * 63 / 100}, "", kMuted, SystemFont::kSmall).value();
        airplay_line_ = scene_.CreateLabel({margin, height * 75 / 100}, "", kText, SystemFont::kSmall).value();
        (void)scene_.CreateLabel({margin, height * 89 / 100}, "VOL- / VOL+  Wi-Fi mode", kMuted, SystemFont::kSmall)
            .value();
    }

    void Refresh() {
        char line[96];
        // Only one Bluetooth device at a time: a paired Mac keeps the iPhone out.
        const bool app_attached = link_state_.connected && link_state_.status_subscribed;
        iphone_.SetText(app_attached           ? "iPhone app: connected"
                        : link_state_.connected ? "Bluetooth busy: open the iPhone app"
                                                : "Pair \"CGotchi\" in iPhone Bluetooth");
        iphone_.SetColor(app_attached ? kGood : kWarn);
        mouse_.SetText(mouse_ready_ ? "Mouse: ready" : "Mouse: waiting for pairing");
        mouse_.SetColor(mouse_ready_ ? kGood : kMuted);
        char address[16];
        if (net_.access_point) {
            (void)snprintf(line, sizeof(line), "%s: hotspot", ModeName(mode_));
            wifi_.SetText(line);
            wifi_detail_.SetText(ssid_);
            (void)snprintf(line, sizeof(line), "password %s", password_);
            wifi_secret_.SetText(line);
        } else if (net_.station_connected) {
            (void)snprintf(line, sizeof(line), "%s: %s", ModeName(mode_), net_.station_ssid);
            wifi_.SetText(line);
            FormatAddress(net_.station_address, address);
            wifi_detail_.SetText(address);
            wifi_secret_.SetText("iPhone: same Wi-Fi");
        } else {
            (void)snprintf(line, sizeof(line), "%s: looking for Wi-Fi", ModeName(mode_));
            wifi_.SetText(line);
            wifi_detail_.SetText(mode_ == WifiMode::kAuto ? "hotspot opens if none is found" : "");
            wifi_secret_.SetText(" ");
        }
        const int airplay = airplay_.State();
        if (airplay == AIRPLAY_STREAMING) {
            const uint32_t fps10 = video_.fps10();
            (void)snprintf(line, sizeof(line), "Mirroring %u.%u fps", static_cast<unsigned>(fps10 / 10U),
                           static_cast<unsigned>(fps10 % 10U));
        } else {
            (void)snprintf(line, sizeof(line), "%s",
                           airplay == AIRPLAY_CONNECTED ? "Mirroring: connecting"
                                                        : "Screen Mirroring > \"ESP32 Agent\"");
        }
        airplay_line_.SetText(line);
        airplay_line_.SetColor(airplay == AIRPLAY_STREAMING ? kGood : kText);
        (void)app_.renderer().Present(scene_);
    }

    micropixel::Application& app_;
    micropixel::HidKeyboard hid_;
    micropixel::BleLink link_;
    micropixel::Network network_;
    micropixel::KVStore storage_;
    VideoLink video_;
    agent::AirPlayHost airplay_;
    micropixel::RendererInfo info_;
    micropixel::Scene scene_;
    micropixel::Timer tick_;
    micropixel::Timer second_;
    micropixel::LabelNode iphone_;
    micropixel::LabelNode mouse_;
    micropixel::LabelNode wifi_;
    micropixel::LabelNode wifi_detail_;
    micropixel::LabelNode wifi_secret_;
    micropixel::LabelNode airplay_line_;
    micropixel::BleLinkState link_state_{};
    micropixel::NetworkState net_{};
    bool mouse_ready_{};
    uint8_t buttons_{};
    WifiMode mode_{WifiMode::kAuto};
    uint64_t mode_since_us_{};
    uint8_t active_link_{};
    uint16_t width_{480U};
    uint16_t height_{1040U};
    uint8_t fps_{5U};
    uint8_t mac_[6]{};
    uint8_t seed_[32]{};
    char ssid_[33]{};
    char password_[65]{};
    char net_value_[micropixel::BleLink::kMaxValue]{};
    Command queue_[kQueueDepth]{};
    uint32_t queue_head_{};
    uint32_t queue_count_{};
    Job job_{};
};

void VideoReply(const char* id, const char* text) { active_agent->ReplyFromVideo(id, text); }

}  // namespace

extern "C" {

void ap_host_on_config(const uint8_t* sps_pps, size_t len) { active_agent->OnVideoConfig(sps_pps, len); }

void ap_host_on_au(const uint8_t* au, size_t len, bool idr) { active_agent->OnVideoAccessUnit(au, len, idr); }

void ap_host_log(const char* msg) { active_agent->Log(msg); }

}  // extern "C"

int main() {
    micropixel::Application app;
    AgentApp agent_app(app);
    app.log().Info("iPhone Agent ready");
    app.Run([&agent_app](const micropixel::Event& event) { return agent_app.Handle(event); });
    return 0;
}
