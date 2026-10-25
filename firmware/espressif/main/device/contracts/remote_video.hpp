#ifndef MICROPIXEL_DEVICE_REMOTE_VIDEO_HPP
#define MICROPIXEL_DEVICE_REMOTE_VIDEO_HPP

#include <cstdint>

#include "abi/micropixel_abi.h"

namespace micropixel::device {

// Called on the receiver task after each decoded frame. Must not block.
using RemoteVideoFrameSink = void (*)(void* context, uint32_t frames_received, uint16_t width, uint16_t height);

// Frames another device sends over the local network. One consumer at a
// time; Start() fails with kInvalidState while already started.
class RemoteVideo {
   public:
    virtual ~RemoteVideo() = default;
    RemoteVideo(const RemoteVideo&) = delete;
    RemoteVideo& operator=(const RemoteVideo&) = delete;

    [[nodiscard]] virtual micropixel_remote_video_state_t State() const = 0;
    [[nodiscard]] virtual int32_t Start(RemoteVideoFrameSink sink, void* context) = 0;
    // Returns once the sink will no longer be called.
    [[nodiscard]] virtual int32_t Stop() = 0;
    // Copies the newest frame centered into `width` x `height` RGB565 pixels;
    // kWouldBlock when there is none newer than the last copy.
    [[nodiscard]] virtual int32_t ReadFrame(uint16_t* pixels, uint16_t width, uint16_t height, bool byte_swapped) = 0;

   protected:
    RemoteVideo() = default;
};

}  // namespace micropixel::device

#endif
