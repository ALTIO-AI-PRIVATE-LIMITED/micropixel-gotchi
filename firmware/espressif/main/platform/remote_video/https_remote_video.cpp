#include "platform/remote_video/https_remote_video.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

extern const char remote_video_page_start[] asm("_binary_remote_video_page_html_start");
extern const char remote_video_page_end[] asm("_binary_remote_video_page_html_end");
extern const uint8_t remote_video_cert_start[] asm("_binary_remote_video_cert_pem_start");
extern const uint8_t remote_video_cert_end[] asm("_binary_remote_video_cert_pem_end");
extern const uint8_t remote_video_key_start[] asm("_binary_remote_video_key_pem_start");
extern const uint8_t remote_video_key_end[] asm("_binary_remote_video_key_pem_end");

namespace micropixel::platform::remote_video {
namespace {

constexpr char kTag[] = "remote_video";
constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
// SENDING clears this long after the last frame.
constexpr int64_t kSendingTimeoutUs = 2000000;

class Lock final {
   public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { (void)xSemaphoreTake(mutex_, portMAX_DELAY); }
    ~Lock() { (void)xSemaphoreGive(mutex_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

   private:
    SemaphoreHandle_t mutex_;
};

}  // namespace

HttpsRemoteVideo::~HttpsRemoteVideo() {
    (void)Stop();
    ReleaseBuffers();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

esp_err_t HttpsRemoteVideo::Initialize() {
    mutex_ = xSemaphoreCreateMutex();
    return mutex_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

micropixel_remote_video_state_t HttpsRemoteVideo::State() const {
    micropixel_remote_video_state_t state{};
    state.size = sizeof(state);
    if (mutex_ == nullptr) {
        return state;
    }
    state.flags = MICROPIXEL_REMOTE_VIDEO_FLAG_AVAILABLE;
    esp_netif_ip_info_t ip{};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0U) {
        state.flags |= MICROPIXEL_REMOTE_VIDEO_FLAG_NETWORK;
        (void)std::snprintf(state.url, sizeof(state.url), "https://" IPSTR, IP2STR(&ip.ip));
    }
    Lock lock(mutex_);
    if (server_ != nullptr) {
        state.flags |= MICROPIXEL_REMOTE_VIDEO_FLAG_STARTED;
    }
    if (frames_received_ != 0U && esp_timer_get_time() - last_frame_us_ < kSendingTimeoutUs) {
        state.flags |= MICROPIXEL_REMOTE_VIDEO_FLAG_SENDING;
    }
    state.frames_received = frames_received_;
    state.frames_dropped = frames_dropped_;
    if (frames_received_ != 0U) {
        state.frame_width = frames_[newest_].width;
        state.frame_height = frames_[newest_].height;
    }
    return state;
}

bool HttpsRemoteVideo::AllocateBuffers() {
    if (jpeg_ == nullptr) {
        jpeg_ = static_cast<uint8_t*>(heap_caps_malloc(kMaxJpegBytes, kPsram));
    }
    for (Frame& frame : frames_) {
        if (frame.pixels == nullptr) {
            frame.pixels =
                static_cast<uint16_t*>(heap_caps_aligned_alloc(64U, kMaxFramePixels * sizeof(uint16_t), kPsram));
        }
    }
    return jpeg_ != nullptr && frames_[0].pixels != nullptr && frames_[1].pixels != nullptr;
}

void HttpsRemoteVideo::ReleaseBuffers() {
    heap_caps_free(jpeg_);
    jpeg_ = nullptr;
    for (Frame& frame : frames_) {
        heap_caps_free(frame.pixels);
        frame = Frame{};
    }
}

int32_t HttpsRemoteVideo::Start(device::RemoteVideoFrameSink sink, void* context) {
    if (mutex_ == nullptr) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if (server_ != nullptr) {
        return MICROPIXEL_STATUS_STALE_STATE;
    }
    if (!AllocateBuffers()) {
        ReleaseBuffers();
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    {
        Lock lock(mutex_);
        sink_ = sink;
        sink_context_ = context;
        frames_received_ = 0U;
        frames_dropped_ = 0U;
        read_frames_ = 0U;
        newest_ = 0U;
    }

    // The IDF default leaves trailing members to zero-initialization.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
#pragma GCC diagnostic pop
    // Each TLS socket holds about 40 KB; one phone needs one or two.
    config.httpd.max_open_sockets = 2;
    config.httpd.max_uri_handlers = 2;
    config.httpd.global_user_ctx = this;
    config.servercert = remote_video_cert_start;
    config.servercert_len = static_cast<size_t>(remote_video_cert_end - remote_video_cert_start);
    config.prvtkey_pem = remote_video_key_start;
    config.prvtkey_len = static_cast<size_t>(remote_video_key_end - remote_video_key_start);
    httpd_handle_t server = nullptr;
    esp_err_t error = httpd_ssl_start(&server, &config);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "HTTPS server failed: %s", esp_err_to_name(error));
        ReleaseBuffers();
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    const httpd_uri_t page{.uri = "/", .method = HTTP_GET, .handler = HandlePage, .user_ctx = this};
    const httpd_uri_t frame{.uri = "/frame", .method = HTTP_POST, .handler = HandleFrame, .user_ctx = this};
    if (httpd_register_uri_handler(server, &page) != ESP_OK || httpd_register_uri_handler(server, &frame) != ESP_OK) {
        (void)httpd_ssl_stop(server);
        ReleaseBuffers();
        return MICROPIXEL_STATUS_INTERNAL;
    }
    Lock lock(mutex_);
    server_ = server;
    ESP_LOGI(kTag, "camera page served on port 443");
    return MICROPIXEL_STATUS_OK;
}

int32_t HttpsRemoteVideo::Stop() {
    if (mutex_ == nullptr) {
        return MICROPIXEL_STATUS_OK;
    }
    httpd_handle_t server = nullptr;
    {
        Lock lock(mutex_);
        server = server_;
        server_ = nullptr;
        sink_ = nullptr;
        sink_context_ = nullptr;
    }
    if (server != nullptr) {
        // Returns after the server task, the only decoder, has exited.
        (void)httpd_ssl_stop(server);
        ReleaseBuffers();
        ESP_LOGI(kTag, "camera page stopped");
    }
    return MICROPIXEL_STATUS_OK;
}

int32_t HttpsRemoteVideo::ReadFrame(uint16_t* pixels, uint16_t width, uint16_t height, bool byte_swapped) {
    if (mutex_ == nullptr) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    Lock lock(mutex_);
    if (server_ == nullptr) {
        return MICROPIXEL_STATUS_STALE_STATE;
    }
    if (frames_received_ == read_frames_) {
        return MICROPIXEL_STATUS_WOULD_BLOCK;
    }
    read_frames_ = frames_received_;
    const Frame& frame = frames_[newest_];
    // Center the frame: crop where it is larger, black where it is smaller.
    const uint32_t copy_width = std::min<uint32_t>(width, frame.width);
    const uint32_t copy_height = std::min<uint32_t>(height, frame.height);
    const uint32_t source_x = (frame.width - copy_width) / 2U;
    const uint32_t source_y = (frame.height - copy_height) / 2U;
    const uint32_t target_x = (width - copy_width) / 2U;
    const uint32_t target_y = (height - copy_height) / 2U;
    if (copy_width != width || copy_height != height) {
        std::memset(pixels, 0, static_cast<size_t>(width) * height * sizeof(uint16_t));
    }
    for (uint32_t row = 0U; row < copy_height; ++row) {
        const uint16_t* source = frame.pixels + (source_y + row) * frame.width + source_x;
        uint16_t* target = pixels + (target_y + row) * width + target_x;
        if (byte_swapped) {
            for (uint32_t x = 0U; x < copy_width; ++x) {
                target[x] = static_cast<uint16_t>((source[x] << 8U) | (source[x] >> 8U));
            }
        } else {
            std::memcpy(target, source, copy_width * sizeof(uint16_t));
        }
    }
    return MICROPIXEL_STATUS_OK;
}

esp_err_t HttpsRemoteVideo::HandlePage(httpd_req_t* request) {
    (void)httpd_resp_set_type(request, "text/html");
    (void)httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, remote_video_page_start, remote_video_page_end - remote_video_page_start);
}

esp_err_t HttpsRemoteVideo::HandleFrame(httpd_req_t* request) {
    return static_cast<HttpsRemoteVideo*>(request->user_ctx)->ReceiveFrame(request);
}

esp_err_t HttpsRemoteVideo::ReceiveFrame(httpd_req_t* request) {
    const size_t length = request->content_len;
    if (length == 0U || length > kMaxJpegBytes) {
        {
            Lock lock(mutex_);
            ++frames_dropped_;
        }
        return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE, "frame too large");
    }
    size_t received = 0U;
    while (received < length) {
        const int count = httpd_req_recv(request, reinterpret_cast<char*>(jpeg_ + received), length - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (count <= 0) {
            return ESP_FAIL;
        }
        received += static_cast<size_t>(count);
    }

    // Only this task writes frames, so the back frame is ours until the swap.
    uint32_t back = 0U;
    {
        Lock lock(mutex_);
        back = newest_ ^ 1U;
    }
    if (!Decode(static_cast<uint32_t>(length), frames_[back])) {
        {
            Lock lock(mutex_);
            ++frames_dropped_;
        }
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "bad frame");
    }
    device::RemoteVideoFrameSink sink = nullptr;
    void* sink_context = nullptr;
    uint32_t received_frames = 0U;
    {
        Lock lock(mutex_);
        newest_ = back;
        received_frames = ++frames_received_;
        last_frame_us_ = esp_timer_get_time();
        sink = sink_;
        sink_context = sink_context_;
    }
    if (sink != nullptr) {
        sink(sink_context, received_frames, frames_[back].width, frames_[back].height);
    }
    (void)httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, nullptr, 0);
}

bool HttpsRemoteVideo::Decode(uint32_t jpeg_bytes, Frame& frame) const {
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) {
        return false;
    }
    jpeg_dec_io_t io{};
    io.inbuf = jpeg_;
    io.inbuf_len = static_cast<int>(jpeg_bytes);
    jpeg_dec_header_info_t header{};
    bool decoded = false;
    if (jpeg_dec_parse_header(decoder, &io, &header) == JPEG_ERR_OK && header.width != 0U && header.height != 0U &&
        static_cast<uint32_t>(header.width) * header.height <= kMaxFramePixels) {
        io.outbuf = reinterpret_cast<uint8_t*>(frame.pixels);
        if (jpeg_dec_process(decoder, &io) == JPEG_ERR_OK) {
            frame.width = header.width;
            frame.height = header.height;
            decoded = true;
        }
    }
    (void)jpeg_dec_close(decoder);
    return decoded;
}

}  // namespace micropixel::platform::remote_video
