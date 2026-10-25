#ifndef MICROPIXEL_SDK_REMOTE_VIDEO_HPP
#define MICROPIXEL_SDK_REMOTE_VIDEO_HPP

#include <stdint.h>

#include "abi/micropixel_abi.h"
#include "sdk/result.hpp"

namespace micropixel {

class Application;

struct RemoteVideoState final {
    // The board can receive video.
    bool available{};
    // Start() succeeded and the camera page is being served.
    bool started{};
    // The device is on Wi-Fi; `url` is the page address.
    bool network{};
    // A frame arrived in the last two seconds.
    bool sending{};
    uint32_t frames_received{};
    uint32_t frames_dropped{};
    uint16_t frame_width{};
    uint16_t frame_height{};
    char url[MICROPIXEL_REMOTE_VIDEO_MAX_URL_BYTES + 1U]{};
};

// Video from another device on the same Wi-Fi. Start() serves a camera page
// at RemoteVideoState::url; a phone browser that opens it streams its camera.
// A RemoteVideoEvent arrives when a newer frame is ready; ReadFrame() copies
// it and re-arms the event. Stops at Stop(), App suspend and App exit.
class RemoteVideo final {
   public:
    static constexpr uint32_t kMaxSide = MICROPIXEL_REMOTE_VIDEO_MAX_SIDE;

    [[nodiscard]] Result<RemoteVideoState> State() const;
    [[nodiscard]] Result<void> Start() const;
    [[nodiscard]] Result<void> Stop() const;
    // Copies the newest frame, centered and cropped (black where it does not
    // cover), into `width` x `height` RGB565 pixels, row-major and 2-byte
    // aligned; `byte_swapped` writes panel order (DirectSurface). kWouldBlock
    // when nothing newer arrived since the last read.
    [[nodiscard]] Result<void> ReadFrame(uint16_t* pixels, uint32_t width, uint32_t height, bool byte_swapped) const;

   private:
    struct CapabilityToken final {
       private:
        constexpr CapabilityToken() = default;
        friend class Application;
    };
    explicit constexpr RemoteVideo(CapabilityToken) noexcept {}
    friend class Application;
};

}  // namespace micropixel

#endif
