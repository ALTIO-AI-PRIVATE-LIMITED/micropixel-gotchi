#ifndef MICROPIXEL_DEVICE_MICROPHONE_HPP
#define MICROPIXEL_DEVICE_MICROPHONE_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Called on the capture task with consecutive mono samples. Must not block.
using MicrophoneSampleSink = void (*)(void* context, const int16_t* samples, uint32_t count);
// Like MicrophoneSampleSink, with interleaved (mic, speaker reference) pairs
// for echo cancellation; `count` is the number of pairs.
using MicrophoneReferenceSink = void (*)(void* context, const int16_t* pairs, uint32_t count);

// Host-owned capture path. One consumer at a time; Start() replaces nothing
// and fails with kInvalidState while already started.
class Microphone {
   public:
    virtual ~Microphone() = default;
    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;

    // 0 when the board has no microphone.
    [[nodiscard]] virtual uint32_t SampleRate() const = 0;
    // `gain_db` 0..37; 0xFF keeps the default.
    [[nodiscard]] virtual int32_t Start(MicrophoneSampleSink sink, void* context, uint8_t gain_db) = 0;
    // Same, plus what the speaker is playing, for boards that loop it back.
    [[nodiscard]] virtual int32_t StartWithReference(MicrophoneReferenceSink, void*, uint8_t) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    // Returns once the sink will no longer be called.
    [[nodiscard]] virtual int32_t Stop() = 0;

   protected:
    Microphone() = default;
};

}  // namespace micropixel::device

#endif
