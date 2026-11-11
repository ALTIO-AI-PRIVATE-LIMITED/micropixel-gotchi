#ifndef MICROPIXEL_SDK_MICROPHONE_HPP
#define MICROPIXEL_SDK_MICROPHONE_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

struct MicrophoneInfo final {
    bool available{};
    bool started{};
    uint32_t sample_rate{};
    // Samples per FRAMES event (20 ms).
    uint32_t frame_samples{};
};

// Built-in microphone: mono int16 at MicrophoneInfo::sample_rate. After
// Start(), a MicrophoneEvent arrives whenever a frame is buffered; Read()
// drains the Host ring and re-arms the event. Capture pauses while the App is
// suspended and ends with Stop() or App exit.
class Microphone final {
   public:
    // Most samples one Read() returns.
    static constexpr uint32_t kMaxReadSamples = MICROPIXEL_MICROPHONE_MAX_READ_SAMPLES;
    // Pass to Start() to keep the Host's default analog gain.
    static constexpr uint8_t kDefaultGain = 0xFFU;

    [[nodiscard]] Result<MicrophoneInfo> Info() const;
    // `gain_db` 0..37, or kDefaultGain.
    [[nodiscard]] Result<void> Start(uint8_t gain_db = kDefaultGain) const;
    [[nodiscard]] Result<void> Stop() const;
    // Copies up to `max_samples` (<= kMaxReadSamples) buffered samples and
    // returns the count; 0 when nothing is buffered.
    [[nodiscard]] Result<uint32_t> Read(int16_t* samples, uint32_t max_samples) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr Microphone(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
