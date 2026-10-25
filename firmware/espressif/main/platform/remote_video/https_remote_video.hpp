#ifndef MICROPIXEL_PLATFORM_REMOTE_VIDEO_HTTPS_REMOTE_VIDEO_HPP
#define MICROPIXEL_PLATFORM_REMOTE_VIDEO_HTTPS_REMOTE_VIDEO_HPP

#include <cstdint>

#include "device/contracts/remote_video.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace micropixel::platform::remote_video {

// Serves a camera page over HTTPS on the Wi-Fi network. A phone browser that
// opens it posts its camera as JPEG frames to /frame, one request per frame;
// each is decoded on the server task into the back of two PSRAM frames and
// becomes the newest frame. iOS only grants a page the camera over HTTPS, so
// the page is served with a self-signed certificate made at build time.
class HttpsRemoteVideo final : public device::RemoteVideo {
   public:
    // Larger frames are dropped; the page sends 296 x 240.
    static constexpr uint32_t kMaxFramePixels = 320U * 320U;
    static constexpr uint32_t kMaxJpegBytes = 64U * 1024U;

    HttpsRemoteVideo() = default;
    ~HttpsRemoteVideo() override;

    [[nodiscard]] esp_err_t Initialize();

    [[nodiscard]] micropixel_remote_video_state_t State() const override;
    [[nodiscard]] int32_t Start(device::RemoteVideoFrameSink sink, void* context) override;
    [[nodiscard]] int32_t Stop() override;
    [[nodiscard]] int32_t ReadFrame(uint16_t* pixels, uint16_t width, uint16_t height, bool byte_swapped) override;

   private:
    struct Frame final {
        uint16_t* pixels{};
        uint16_t width{};
        uint16_t height{};
    };

    static esp_err_t HandlePage(httpd_req_t* request);
    static esp_err_t HandleFrame(httpd_req_t* request);
    [[nodiscard]] esp_err_t ReceiveFrame(httpd_req_t* request);
    [[nodiscard]] bool Decode(uint32_t jpeg_bytes, Frame& frame) const;
    [[nodiscard]] bool AllocateBuffers();
    void ReleaseBuffers();

    mutable SemaphoreHandle_t mutex_{};
    httpd_handle_t server_{};
    uint8_t* jpeg_{};
    Frame frames_[2]{};
    // Guarded by mutex_.
    uint32_t newest_{};
    uint32_t frames_received_{};
    uint32_t frames_dropped_{};
    uint32_t read_frames_{};
    int64_t last_frame_us_{};
    device::RemoteVideoFrameSink sink_{};
    void* sink_context_{};
};

}  // namespace micropixel::platform::remote_video

#endif
