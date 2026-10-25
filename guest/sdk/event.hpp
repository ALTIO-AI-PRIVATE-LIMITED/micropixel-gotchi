#ifndef MICROPIXEL_SDK_EVENT_HPP
#define MICROPIXEL_SDK_EVENT_HPP

#include <stdint.h>

#include "sdk/devices.hpp"
#include "sdk/geometry.hpp"
#include "sdk/types.hpp"

namespace micropixel {

class Application;
class Playback;
class Timer;
class GpioInput;
class Haptic;
class DirectSurface;
class PcmStream;

enum class EventType : uint16_t {
    kUnknown,
    kResume,
    kStop,
    kTimer,
    kTouch,
    kKey,
    kAudioPlayback,
    kDeviceAdded,
    kDeviceRemoved,
    kGpioEdge,
    kHapticFinished,
    kSurfaceReleased,
    kPcmStreamLowWater,
    // Input 1.1: analog gamepad axis.
    kAxis,
    // Microphone 1.0: a captured frame is ready to Read().
    kMicrophone,
    // Peer Radio 1.0.
    kPeerPacket,
    kPeerState,
    kPeerCandidate,
    // Remote Video 1.0: a newer frame is ready to ReadFrame().
    kRemoteVideoFrame,
};

enum class TouchPhase : uint8_t {
    kDown,
    kMove,
    kUp,
    kCancel,
};

class TouchEvent final {
   public:
    constexpr TouchEvent(const TouchEvent&) = default;
    constexpr TouchEvent& operator=(const TouchEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr TouchPhase phase() const { return phase_; }
    [[nodiscard]] constexpr uint32_t id() const { return id_; }
    [[nodiscard]] constexpr int32_t x() const { return x_; }
    [[nodiscard]] constexpr int32_t y() const { return y_; }
    [[nodiscard]] constexpr Point position() const { return Point{x_, y_}; }
    [[nodiscard]] constexpr bool has_pressure() const { return has_pressure_; }
    [[nodiscard]] constexpr uint16_t pressure_per_mille() const { return pressure_per_mille_; }
    [[nodiscard]] constexpr TouchEvent WithPosition(Point position) const {
        return TouchEvent{timestamp_, phase_, id_, position.x, position.y, has_pressure_, pressure_per_mille_};
    }

   private:
    constexpr TouchEvent(TimePoint timestamp, TouchPhase phase, uint32_t id, int32_t x, int32_t y, bool has_pressure,
                         uint16_t pressure_per_mille)
        : timestamp_(timestamp),
          phase_(phase),
          id_(id),
          x_(x),
          y_(y),
          has_pressure_(has_pressure),
          pressure_per_mille_(pressure_per_mille) {}

    TimePoint timestamp_{};
    TouchPhase phase_{TouchPhase::kCancel};
    uint32_t id_{};
    int32_t x_{};
    int32_t y_{};
    bool has_pressure_{};
    uint16_t pressure_per_mille_{};

    friend class Application;
    friend class Event;
};

enum class KeyCode : uint16_t {
    // Directional controls and logical actions are mapped by the Host.
    kUp = 1,
    kDown = 2,
    kLeft = 3,
    kRight = 4,
    kConfirm = 5,
    kBack = 6,
    kMenu = 7,

    // Face buttons are named by physical position so their meaning does not
    // depend on Xbox/Nintendo/PlayStation label conventions.
    kSouth = 8,
    kEast = 9,
    kWest = 10,
    kNorth = 11,
};

enum class KeyPhase : uint8_t {
    kDown,
    kUp,
    kRepeat,
    kCancel,
};

class KeyEvent final {
   public:
    constexpr KeyEvent(const KeyEvent&) = default;
    constexpr KeyEvent& operator=(const KeyEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr KeyCode code() const { return code_; }
    [[nodiscard]] constexpr KeyPhase phase() const { return phase_; }
    [[nodiscard]] constexpr uint32_t repeat_count() const { return repeat_count_; }

   private:
    constexpr KeyEvent(TimePoint timestamp, KeyCode code, KeyPhase phase, uint32_t repeat_count)
        : timestamp_(timestamp), code_(code), phase_(phase), repeat_count_(repeat_count) {}

    TimePoint timestamp_{};
    KeyCode code_{KeyCode::kConfirm};
    KeyPhase phase_{KeyPhase::kCancel};
    uint32_t repeat_count_{};

    friend class Application;
    friend class Event;
};

// Analog gamepad axes, named by position like the face keys. Sticks report
// -1..1 (positive = right / down, screen convention), triggers 0..1.
enum class GamepadAxis : uint16_t {
    kLeftX = 1,
    kLeftY = 2,
    kRightX = 3,
    kRightY = 4,
    kLeftTrigger = 5,
    kRightTrigger = 6,
};

class AxisEvent final {
   public:
    constexpr AxisEvent(const AxisEvent&) = default;
    constexpr AxisEvent& operator=(const AxisEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr GamepadAxis axis() const { return axis_; }
    // Normalized: -1..1 for sticks, 0..1 for triggers.
    [[nodiscard]] constexpr float value() const { return value_; }
    // The gamepad the axis belongs to; invalid when the Host cannot attribute it.
    [[nodiscard]] constexpr DeviceId device() const { return device_; }

   private:
    constexpr AxisEvent() = default;
    constexpr AxisEvent(TimePoint timestamp, GamepadAxis axis, float value, DeviceId device)
        : timestamp_(timestamp), axis_(axis), value_(value), device_(device) {}

    TimePoint timestamp_{};
    GamepadAxis axis_{GamepadAxis::kLeftX};
    float value_{};
    DeviceId device_{};
    friend class Application;
    friend class Event;
};

class TimerEvent final {
   public:
    constexpr TimerEvent(const TimerEvent&) = default;
    constexpr TimerEvent& operator=(const TimerEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr Duration delta() const { return delta_; }
    [[nodiscard]] constexpr uint32_t missed_count() const { return missed_count_; }

   private:
    constexpr TimerEvent() = default;
    constexpr TimerEvent(TimePoint timestamp, Duration delta, uint32_t missed_count, uint32_t source)
        : timestamp_(timestamp), delta_(delta), missed_count_(missed_count), source_(source) {}

    TimePoint timestamp_{};
    Duration delta_{};
    uint32_t missed_count_{};
    uint32_t source_{};

    friend class Application;
    friend class Event;
    friend class Timer;
};

class AudioPlaybackEvent final {
   public:
    constexpr AudioPlaybackEvent(const AudioPlaybackEvent&) = default;
    constexpr AudioPlaybackEvent& operator=(const AudioPlaybackEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr bool succeeded() const { return succeeded_; }

   private:
    constexpr AudioPlaybackEvent() = default;
    constexpr AudioPlaybackEvent(TimePoint timestamp, bool succeeded, uint32_t source)
        : timestamp_(timestamp), succeeded_(succeeded), source_(source) {}

    TimePoint timestamp_{};
    bool succeeded_{};
    uint32_t source_{};

    friend class Application;
    friend class Event;
    friend class Playback;
};

// A Guest PCM stream drained to its low-water mark; `free_frames` is how much
// the Guest may write right away.
class PcmStreamEvent final {
   public:
    constexpr PcmStreamEvent(const PcmStreamEvent&) = default;
    constexpr PcmStreamEvent& operator=(const PcmStreamEvent&) = default;

    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr uint32_t free_frames() const { return free_frames_; }

   private:
    constexpr PcmStreamEvent() = default;
    constexpr PcmStreamEvent(TimePoint timestamp, uint32_t free_frames, uint32_t source)
        : timestamp_(timestamp), free_frames_(free_frames), source_(source) {}

    TimePoint timestamp_{};
    uint32_t free_frames_{};
    uint32_t source_{};

    friend class Application;
    friend class Event;
    friend class PcmStream;
};

class DeviceEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr DeviceId id() const { return device_; }
    [[nodiscard]] constexpr DeviceKind kind() const { return kind_; }
    [[nodiscard]] constexpr uint32_t generation() const { return generation_; }

   private:
    constexpr DeviceEvent() = default;
    constexpr DeviceEvent(TimePoint timestamp, DeviceId device, DeviceKind kind, uint32_t generation)
        : timestamp_(timestamp), device_(device), kind_(kind), generation_(generation) {}

    TimePoint timestamp_{};
    DeviceId device_{};
    DeviceKind kind_{DeviceKind::kAny};
    uint32_t generation_{};
    friend class Application;
    friend class Event;
};

enum class GpioEdge : uint16_t {
    kRising = 1,
    kFalling = 2,
};

class GpioEdgeEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr bool value() const { return value_; }
    [[nodiscard]] constexpr GpioEdge edge() const { return edge_; }

   private:
    constexpr GpioEdgeEvent() = default;
    constexpr GpioEdgeEvent(TimePoint timestamp, bool value, GpioEdge edge, uint32_t source)
        : timestamp_(timestamp), value_(value), edge_(edge), source_(source) {}

    TimePoint timestamp_{};
    bool value_{};
    GpioEdge edge_{GpioEdge::kRising};
    uint32_t source_{};
    friend class Application;
    friend class Event;
    friend class GpioInput;
};

class HapticEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }

   private:
    constexpr HapticEvent() = default;
    constexpr HapticEvent(TimePoint timestamp, uint32_t source) : timestamp_(timestamp), source_(source) {}

    TimePoint timestamp_{};
    uint32_t source_{};
    friend class Application;
    friend class Event;
    friend class Haptic;
};

// The Host finished reading a presented DirectSurface buffer; the Guest may
// render into it again.
class SurfaceReleasedEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr uint32_t buffer_index() const { return buffer_index_; }

   private:
    constexpr SurfaceReleasedEvent() = default;
    constexpr SurfaceReleasedEvent(TimePoint timestamp, uint32_t buffer_index, uint32_t source)
        : timestamp_(timestamp), buffer_index_(buffer_index), source_(source) {}

    TimePoint timestamp_{};
    uint32_t buffer_index_{};
    uint32_t source_{};
    friend class Application;
    friend class Event;
    friend class DirectSurface;
};

// The microphone buffered at least one frame since the last Read().
class MicrophoneEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr uint32_t available_samples() const { return available_samples_; }
    [[nodiscard]] constexpr uint32_t dropped_samples() const { return dropped_samples_; }

   private:
    constexpr MicrophoneEvent() = default;
    constexpr MicrophoneEvent(TimePoint timestamp, uint32_t available_samples, uint32_t dropped_samples)
        : timestamp_(timestamp), available_samples_(available_samples), dropped_samples_(dropped_samples) {}

    TimePoint timestamp_{};
    uint32_t available_samples_{};
    uint32_t dropped_samples_{};
    friend class Application;
    friend class Event;
};

// Remote Video received a newer frame since the last ReadFrame().
class RemoteVideoEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr uint32_t frames_received() const { return frames_received_; }
    [[nodiscard]] constexpr uint16_t frame_width() const { return frame_width_; }
    [[nodiscard]] constexpr uint16_t frame_height() const { return frame_height_; }

   private:
    constexpr RemoteVideoEvent() = default;
    constexpr RemoteVideoEvent(TimePoint timestamp, uint32_t frames_received, uint16_t frame_width,
                               uint16_t frame_height)
        : timestamp_(timestamp),
          frames_received_(frames_received),
          frame_width_(frame_width),
          frame_height_(frame_height) {}

    TimePoint timestamp_{};
    uint32_t frames_received_{};
    uint16_t frame_width_{};
    uint16_t frame_height_{};
    friend class Application;
    friend class Event;
};

enum class PeerRadioEventKind : uint8_t {
    // Packets are queued; call PeerRadio::Receive() until it returns length 0.
    kPacket,
    // Link flags or channel changed; PeerRadio::State() has the details.
    kState,
    // A nearby device is pairing too; show code() and offer Accept(address()).
    kCandidate,
};

class PeerRadioEvent final {
   public:
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }
    [[nodiscard]] constexpr PeerRadioEventKind kind() const { return kind_; }
    // kCandidate: the 4-digit code both devices display.
    [[nodiscard]] constexpr uint16_t code() const { return code_; }
    [[nodiscard]] constexpr const uint8_t (&address() const)[6] { return address_; }
    [[nodiscard]] constexpr int8_t rssi() const { return rssi_; }
    // kState: MICROPIXEL_PEER_RADIO_STATE_* flags.
    [[nodiscard]] constexpr uint32_t flags() const { return flags_; }
    [[nodiscard]] constexpr uint8_t channel() const { return channel_; }

   private:
    constexpr PeerRadioEvent() = default;

    TimePoint timestamp_{};
    PeerRadioEventKind kind_{PeerRadioEventKind::kPacket};
    uint16_t code_{};
    uint8_t address_[6]{};
    int8_t rssi_{};
    uint8_t channel_{};
    uint32_t flags_{};
    friend class Application;
    friend class Event;
};

class Event final {
   public:
    constexpr Event() = default;
    constexpr Event(const Event&) = default;
    constexpr Event& operator=(const Event&) = default;
    constexpr Event(Event&&) = default;
    constexpr Event& operator=(Event&&) = default;

    [[nodiscard]] constexpr EventType type() const { return type_; }
    [[nodiscard]] constexpr TimePoint timestamp() const { return timestamp_; }

    // Returns a view only when this is a TimerEvent emitted by source.
    // The pointer remains valid until this Event is destroyed or reassigned.
    [[nodiscard]] const TimerEvent* TimerFrom(const Timer& source) const;
    [[nodiscard]] const AudioPlaybackEvent* PlaybackFrom(const Playback& source) const;
    [[nodiscard]] const GpioEdgeEvent* EdgeFrom(const GpioInput& source) const;
    [[nodiscard]] const HapticEvent* HapticFrom(const Haptic& source) const;
    [[nodiscard]] const SurfaceReleasedEvent* ReleasedFrom(const DirectSurface& source) const;
    [[nodiscard]] const PcmStreamEvent* LowWaterFrom(const PcmStream& source) const;
    [[nodiscard]] constexpr const TouchEvent* touch() const { return type_ == EventType::kTouch ? &touch_ : nullptr; }
    [[nodiscard]] constexpr const KeyEvent* key() const { return type_ == EventType::kKey ? &key_ : nullptr; }
    [[nodiscard]] constexpr const AxisEvent* axis() const { return type_ == EventType::kAxis ? &axis_ : nullptr; }
    [[nodiscard]] constexpr const MicrophoneEvent* microphone() const {
        return type_ == EventType::kMicrophone ? &microphone_ : nullptr;
    }
    [[nodiscard]] constexpr const RemoteVideoEvent* remote_video() const {
        return type_ == EventType::kRemoteVideoFrame ? &remote_video_ : nullptr;
    }
    [[nodiscard]] constexpr const PeerRadioEvent* peer_radio() const {
        return type_ == EventType::kPeerPacket || type_ == EventType::kPeerState ||
                       type_ == EventType::kPeerCandidate
                   ? &peer_radio_
                   : nullptr;
    }
    // True when the Runtime-owned gamepad (Application::gamepad()) took this
    // touch, key or axis before it reached the App; UI code skips such events.
    [[nodiscard]] constexpr bool gamepad_handled() const { return gamepad_handled_; }
    [[nodiscard]] constexpr const DeviceEvent* device() const {
        return type_ == EventType::kDeviceAdded || type_ == EventType::kDeviceRemoved ? &device_ : nullptr;
    }

   private:
    [[nodiscard]] constexpr const TimerEvent* timer() const { return type_ == EventType::kTimer ? &timer_ : nullptr; }
    [[nodiscard]] constexpr const AudioPlaybackEvent* audio_playback() const {
        return type_ == EventType::kAudioPlayback ? &audio_playback_ : nullptr;
    }
    [[nodiscard]] constexpr const GpioEdgeEvent* gpio_edge() const {
        return type_ == EventType::kGpioEdge ? &gpio_edge_ : nullptr;
    }
    [[nodiscard]] constexpr const HapticEvent* haptic() const {
        return type_ == EventType::kHapticFinished ? &haptic_ : nullptr;
    }
    [[nodiscard]] constexpr const SurfaceReleasedEvent* surface_released() const {
        return type_ == EventType::kSurfaceReleased ? &surface_released_ : nullptr;
    }
    [[nodiscard]] constexpr const PcmStreamEvent* pcm_stream() const {
        return type_ == EventType::kPcmStreamLowWater ? &pcm_stream_ : nullptr;
    }

    explicit constexpr Event(TimePoint timestamp) : type_(EventType::kUnknown), timestamp_(timestamp) {}

    constexpr Event(EventType type, TimePoint timestamp) : type_(type), timestamp_(timestamp) {}

    explicit constexpr Event(TimerEvent timer)
        : type_(EventType::kTimer), timestamp_(timer.timestamp()), timer_(timer) {}

    explicit constexpr Event(TouchEvent touch)
        : type_(EventType::kTouch), timestamp_(touch.timestamp()), touch_(touch) {}

    explicit constexpr Event(KeyEvent key) : type_(EventType::kKey), timestamp_(key.timestamp()), key_(key) {}

    explicit constexpr Event(AxisEvent axis) : type_(EventType::kAxis), timestamp_(axis.timestamp()), axis_(axis) {}

    explicit constexpr Event(AudioPlaybackEvent playback)
        : type_(EventType::kAudioPlayback), timestamp_(playback.timestamp()), audio_playback_(playback) {}

    constexpr Event(EventType type, DeviceEvent device)
        : type_(type), timestamp_(device.timestamp()), device_(device) {}

    explicit constexpr Event(GpioEdgeEvent gpio_edge)
        : type_(EventType::kGpioEdge), timestamp_(gpio_edge.timestamp()), gpio_edge_(gpio_edge) {}

    explicit constexpr Event(HapticEvent haptic)
        : type_(EventType::kHapticFinished), timestamp_(haptic.timestamp()), haptic_(haptic) {}

    explicit constexpr Event(SurfaceReleasedEvent released)
        : type_(EventType::kSurfaceReleased), timestamp_(released.timestamp()), surface_released_(released) {}

    explicit constexpr Event(RemoteVideoEvent remote_video)
        : type_(EventType::kRemoteVideoFrame), timestamp_(remote_video.timestamp()), remote_video_(remote_video) {}

    explicit constexpr Event(MicrophoneEvent microphone)
        : type_(EventType::kMicrophone), timestamp_(microphone.timestamp()), microphone_(microphone) {}

    constexpr Event(EventType type, PeerRadioEvent peer_radio)
        : type_(type), timestamp_(peer_radio.timestamp()), peer_radio_(peer_radio) {}

    explicit constexpr Event(PcmStreamEvent low_water)
        : type_(EventType::kPcmStreamLowWater), timestamp_(low_water.timestamp()), pcm_stream_(low_water) {}

    EventType type_{EventType::kUnknown};
    TimePoint timestamp_{};
    TimerEvent timer_{};
    TouchEvent touch_{TimePoint{}, TouchPhase::kCancel, 0U, 0, 0, false, 0U};
    KeyEvent key_{TimePoint{}, KeyCode::kConfirm, KeyPhase::kCancel, 0U};
    AxisEvent axis_{};
    AudioPlaybackEvent audio_playback_{};
    DeviceEvent device_{};
    GpioEdgeEvent gpio_edge_{};
    HapticEvent haptic_{};
    SurfaceReleasedEvent surface_released_{};
    PcmStreamEvent pcm_stream_{};
    MicrophoneEvent microphone_{};
    RemoteVideoEvent remote_video_{};
    PeerRadioEvent peer_radio_{};
    bool gamepad_handled_{};
    friend class Application;
};

}  // namespace micropixel

#endif
