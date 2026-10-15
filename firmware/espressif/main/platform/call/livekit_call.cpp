#include "platform/call/livekit_call.hpp"

#include <cstdio>
#include <cstring>

#include "av_render_default.h"
#include "cJSON.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_capture_defaults.h"
#include "esp_codec_dev.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "livekit.h"

namespace micropixel::platform::call {
namespace {

constexpr char kTag[] = "livekit_call";
constexpr uint32_t kSpeakerToken = 0x43414c4cU;  // "CALL"
constexpr uint32_t kHttpTimeoutMs = 10000U;
constexpr int kMaxResponseBytes = 2048;
constexpr TickType_t kMicrophoneWait = pdMS_TO_TICKS(100);

bool Configured() { return CONFIG_MICROPIXEL_CALL_SERVER_URL[0] != '\0'; }

LiveKitCall& Owner(const audio_codec_data_if_t* port) {
    return *reinterpret_cast<const LiveKitCall::Port*>(port)->owner;
}

// esp_codec_dev data interface over the call's rings: no codec registers,
// just bytes. The format is fixed (16-bit, the engine rate) and not checked.
int PortOpen(const audio_codec_data_if_t*, void*, int) { return ESP_CODEC_DEV_OK; }
bool PortIsOpen(const audio_codec_data_if_t*) { return true; }
int PortEnable(const audio_codec_data_if_t*, esp_codec_dev_type_t, bool) { return ESP_CODEC_DEV_OK; }
int PortSetFormat(const audio_codec_data_if_t*, esp_codec_dev_type_t, esp_codec_dev_sample_info_t*) {
    return ESP_CODEC_DEV_OK;
}
int PortRead(const audio_codec_data_if_t* port, uint8_t* data, int size) {
    return Owner(port).ReadMicrophone(data, size);
}
int PortWrite(const audio_codec_data_if_t* port, uint8_t* data, int size) {
    return Owner(port).WriteSpeaker(data, size);
}
int PortClose(const audio_codec_data_if_t*) { return ESP_CODEC_DEV_OK; }

void InitPort(LiveKitCall::Port& port, LiveKitCall* owner) {
    port.base.open = PortOpen;
    port.base.is_open = PortIsOpen;
    port.base.enable = PortEnable;
    port.base.set_fmt = PortSetFormat;
    port.base.read = PortRead;
    port.base.write = PortWrite;
    port.base.close = PortClose;
    port.owner = owner;
}

bool CopyField(const cJSON* root, const char* name, char* destination, size_t capacity) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || std::strlen(item->valuestring) >= capacity) {
        return false;
    }
    std::memcpy(destination, item->valuestring, std::strlen(item->valuestring) + 1U);
    return true;
}

// POSTs JSON to the call server. Returns the HTTP status (-1 on transport
// failure); `response`, when given, receives a heap body the caller frees.
int PostJson(const char* path, const char* body, char** response) {
    char url[192];
    std::snprintf(url, sizeof(url), "%s%s", CONFIG_MICROPIXEL_CALL_SERVER_URL, path);
    esp_http_client_config_t config{};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = kHttpTimeoutMs;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return -1;
    }
    char authorization[160];
    std::snprintf(authorization, sizeof(authorization), "Bearer %s", CONFIG_MICROPIXEL_CALL_DEVICE_SECRET);
    (void)esp_http_client_set_header(client, "Authorization", authorization);
    (void)esp_http_client_set_header(client, "Content-Type", "application/json");
    int status = -1;
    const int length = static_cast<int>(std::strlen(body));
    if (esp_http_client_open(client, length) == ESP_OK && esp_http_client_write(client, body, length) == length &&
        esp_http_client_fetch_headers(client) >= 0) {
        status = esp_http_client_get_status_code(client);
        if (response != nullptr) {
            char* buffer = static_cast<char*>(heap_caps_calloc(1U, kMaxResponseBytes, MALLOC_CAP_SPIRAM));
            int total = 0;
            int read = 0;
            while (buffer != nullptr && total < kMaxResponseBytes - 1 &&
                   (read = esp_http_client_read(client, buffer + total, kMaxResponseBytes - 1 - total)) > 0) {
                total += read;
            }
            *response = buffer;
        }
    }
    (void)esp_http_client_cleanup(client);
    return status;
}

}  // namespace

// LiveKit calls these on its own tasks; they only post to the call task.
struct RoomCallbacks final {
    static void State(livekit_connection_state_t state, void* context) {
        ESP_LOGI(kTag, "room %s", livekit_connection_state_str(state));
        static_cast<LiveKitCall*>(context)->Post(LiveKitCall::Command::kRoomState, state);
    }
    static void Participant(const livekit_participant_info_t* info, void* context) {
        auto& self = *static_cast<LiveKitCall*>(context);
        // The info is freed when this returns, so compare here. identity_ is
        // fixed while a room exists.
        if (info->identity == nullptr || std::strcmp(info->identity, self.identity_) == 0) {
            return;
        }
        if (info->state == LIVEKIT_PARTICIPANT_STATE_ACTIVE) {
            self.Post(LiveKitCall::Command::kRemoteJoined);
        } else if (info->state == LIVEKIT_PARTICIPANT_STATE_DISCONNECTED) {
            self.Post(LiveKitCall::Command::kRemoteLeft);
        }
    }
};

esp_err_t LiveKitCall::Initialize() {
    if (task_ != nullptr) {
        return ESP_OK;
    }
    queue_ = xQueueCreate(16, sizeof(Message));
    sink_mutex_ = xSemaphoreCreateMutex();
    speaker_ring_ = xStreamBufferCreateWithCaps(kSpeakerRingBytes, 1U, MALLOC_CAP_SPIRAM);
    microphone_ring_ = xStreamBufferCreateWithCaps(kMicrophoneRingBytes, 1U, MALLOC_CAP_SPIRAM);
    if (queue_ == nullptr || sink_mutex_ == nullptr || speaker_ring_ == nullptr || microphone_ring_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    InitPort(speaker_port_, this);
    InitPort(microphone_port_, this);
    if (xTaskCreatePinnedToCoreWithCaps(TaskEntry, "call", 8192, this, 5, &task_, tskNO_AFFINITY, MALLOC_CAP_SPIRAM) !=
        pdPASS) {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "ready, call server %s", Configured() ? CONFIG_MICROPIXEL_CALL_SERVER_URL : "not configured");
    return ESP_OK;
}

micropixel_call_state_t LiveKitCall::State() const {
    micropixel_call_state_t state{};
    state.size = sizeof(state);
    state.flags = task_ != nullptr && Configured() ? MICROPIXEL_CALL_FLAG_AVAILABLE : 0U;
    state.phase = phase_.load(std::memory_order_acquire);
    state.end_reason = end_reason_.load(std::memory_order_acquire);
    if (state.phase == MICROPIXEL_CALL_PHASE_CONNECTED) {
        state.connected_ms =
            static_cast<uint32_t>((esp_timer_get_time() - phase_since_us_.load(std::memory_order_acquire)) / 1000);
    }
    return state;
}

int32_t LiveKitCall::Dial(const char* callee, device::CallStateSink sink, void* context) {
    if (task_ == nullptr || !Configured()) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if (callee == nullptr || callee[0] == '\0' || std::strlen(callee) > MICROPIXEL_CALL_MAX_CALLEE_BYTES) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    uint8_t idle = MICROPIXEL_CALL_PHASE_IDLE;
    if (!phase_.compare_exchange_strong(idle, MICROPIXEL_CALL_PHASE_DIALING, std::memory_order_acq_rel)) {
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    (void)xSemaphoreTake(sink_mutex_, portMAX_DELAY);
    sink_ = sink;
    sink_context_ = context;
    (void)xSemaphoreGive(sink_mutex_);
    std::snprintf(callee_, sizeof(callee_), "%s", callee);
    hang_up_requested_.store(false, std::memory_order_release);
    Post(Command::kDial);
    return MICROPIXEL_STATUS_OK;
}

int32_t LiveKitCall::HangUp() {
    if (task_ == nullptr) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    hang_up_requested_.store(true, std::memory_order_release);
    Post(Command::kHangUp);
    return MICROPIXEL_STATUS_OK;
}

void LiveKitCall::Release() {
    if (task_ == nullptr) {
        return;
    }
    (void)xSemaphoreTake(sink_mutex_, portMAX_DELAY);
    sink_ = nullptr;
    sink_context_ = nullptr;
    (void)xSemaphoreGive(sink_mutex_);
    (void)HangUp();
}

void LiveKitCall::Post(Command command, int32_t value) {
    const Message message{command, value};
    if (xQueueSend(queue_, &message, 0U) != pdTRUE) {
        ESP_LOGW(kTag, "command queue full, dropped %d", static_cast<int>(command));
    }
}

void LiveKitCall::TaskEntry(void* argument) { static_cast<LiveKitCall*>(argument)->Run(); }

void LiveKitCall::Run() {
    for (;;) {
        Message message{};
        if (xQueueReceive(queue_, &message, pdMS_TO_TICKS(500)) == pdTRUE) {
            Handle(message);
        }
        const int64_t elapsed_ms = (esp_timer_get_time() - phase_since_us_.load(std::memory_order_acquire)) / 1000;
        if (phase_.load(std::memory_order_acquire) == MICROPIXEL_CALL_PHASE_RINGING && elapsed_ms > kRingTimeoutMs) {
            EndCall(MICROPIXEL_CALL_END_NO_ANSWER);
        }
    }
}

void LiveKitCall::Handle(const Message& message) {
    const uint8_t phase = phase_.load(std::memory_order_acquire);
    switch (message.command) {
        case Command::kDial:
            StartCall();
            return;
        case Command::kHangUp:
            if (room_ != nullptr) {
                EndCall(MICROPIXEL_CALL_END_HUNG_UP);
            }
            return;
        case Command::kRemoteJoined:
            if (phase == MICROPIXEL_CALL_PHASE_RINGING) {
                SetPhase(MICROPIXEL_CALL_PHASE_CONNECTED, MICROPIXEL_CALL_END_NONE);
            }
            return;
        case Command::kRemoteLeft:
            if (room_ != nullptr) {
                EndCall(phase == MICROPIXEL_CALL_PHASE_CONNECTED ? MICROPIXEL_CALL_END_REMOTE_HUNG_UP
                                                                 : MICROPIXEL_CALL_END_NO_ANSWER);
            }
            return;
        case Command::kRoomState:
            if (message.value == LIVEKIT_CONNECTION_STATE_CONNECTED) {
                room_connected_ = true;
            }
            // The server deletes the room when the phone declines or hangs up.
            if (room_ != nullptr && (message.value == LIVEKIT_CONNECTION_STATE_FAILED ||
                                     (message.value == LIVEKIT_CONNECTION_STATE_DISCONNECTED && room_connected_))) {
                EndCall(phase == MICROPIXEL_CALL_PHASE_CONNECTED ? MICROPIXEL_CALL_END_REMOTE_HUNG_UP
                        : room_connected_                        ? MICROPIXEL_CALL_END_NO_ANSWER
                                                                 : MICROPIXEL_CALL_END_FAILED);
            }
            return;
    }
}

void LiveKitCall::StartCall() {
    SetPhase(MICROPIXEL_CALL_PHASE_DIALING, MICROPIXEL_CALL_END_NONE);
    call_id_[0] = '\0';
    if (!EnsureMedia() || !RequestRoom()) {
        SetPhase(MICROPIXEL_CALL_PHASE_IDLE, MICROPIXEL_CALL_END_FAILED);
        return;
    }
    if (hang_up_requested_.load(std::memory_order_acquire)) {
        NotifyServerEnded();
        SetPhase(MICROPIXEL_CALL_PHASE_IDLE, MICROPIXEL_CALL_END_HUNG_UP);
        return;
    }
    if (!StartAudio()) {
        NotifyServerEnded();
        SetPhase(MICROPIXEL_CALL_PHASE_IDLE, MICROPIXEL_CALL_END_FAILED);
        return;
    }

    livekit_room_options_t options{};
    options.publish.kind = LIVEKIT_MEDIA_TYPE_AUDIO;
    options.publish.audio_encode.codec = LIVEKIT_AUDIO_CODEC_OPUS;
    options.publish.audio_encode.sample_rate = 16000U;
    options.publish.audio_encode.channel_count = 1U;
    options.publish.capturer = static_cast<esp_capture_handle_t>(capturer_);
    options.subscribe.kind = LIVEKIT_MEDIA_TYPE_AUDIO;
    options.subscribe.renderer = static_cast<av_render_handle_t>(renderer_);
    options.on_state_changed = RoomCallbacks::State;
    options.on_participant_info = RoomCallbacks::Participant;
    options.ctx = this;
    room_connected_ = false;
    livekit_room_handle_t room = nullptr;
    if (livekit_room_create(&room, &options) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(kTag, "room create failed");
        StopAudio();
        NotifyServerEnded();
        SetPhase(MICROPIXEL_CALL_PHASE_IDLE, MICROPIXEL_CALL_END_FAILED);
        return;
    }
    room_ = room;
    if (livekit_room_connect(room, url_, token_) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(kTag, "room connect failed");
        EndCall(MICROPIXEL_CALL_END_FAILED);
        return;
    }
    SetPhase(MICROPIXEL_CALL_PHASE_RINGING, MICROPIXEL_CALL_END_NONE);
}

void LiveKitCall::EndCall(uint8_t reason) {
    if (room_ != nullptr) {
        auto room = static_cast<livekit_room_handle_t>(room_);
        (void)livekit_room_close(room);
        (void)livekit_room_destroy(room);
        room_ = nullptr;
    }
    StopAudio();
    SetPhase(MICROPIXEL_CALL_PHASE_IDLE, reason);
    // After the phase change, so the App is not held up by the round trip.
    NotifyServerEnded();
}

void LiveKitCall::SetPhase(uint8_t phase, uint8_t reason) {
    phase_since_us_.store(esp_timer_get_time(), std::memory_order_release);
    end_reason_.store(reason, std::memory_order_release);
    phase_.store(phase, std::memory_order_release);
    ESP_LOGI(kTag, "phase %u reason %u", phase, reason);
    (void)xSemaphoreTake(sink_mutex_, portMAX_DELAY);
    if (sink_ != nullptr) {
        sink_(sink_context_, phase, reason);
    }
    (void)xSemaphoreGive(sink_mutex_);
}

bool LiveKitCall::EnsureMedia() {
    if (capturer_ != nullptr && renderer_ != nullptr) {
        return true;
    }
    micropixel_audio_info_t info{};
    if (audio_.GetInfo(info) == MICROPIXEL_STATUS_OK && info.sample_rate != 0U) {
        sample_rate_ = info.sample_rate;
    }
    (void)livekit_system_init();
    (void)esp_audio_enc_register_default();
    (void)esp_audio_dec_register_default();

    // Capture: (mic, speaker reference) pairs through esp-sr echo cancellation.
    esp_codec_dev_cfg_t input_config{};
    input_config.dev_type = ESP_CODEC_DEV_TYPE_IN;
    input_config.data_if = &microphone_port_.base;
    esp_codec_dev_handle_t input = esp_codec_dev_new(&input_config);
    esp_capture_audio_aec_src_cfg_t aec_config{};
    aec_config.record_handle = input;
    aec_config.channel = 2U;
    aec_config.channel_mask = 0x3U;
    esp_capture_audio_src_if_t* source = input != nullptr ? esp_capture_new_audio_aec_src(&aec_config) : nullptr;
    esp_capture_cfg_t capture_config{};
    capture_config.sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO;
    capture_config.audio_src = source;
    esp_capture_handle_t capturer = nullptr;
    if (source != nullptr) {
        (void)esp_capture_open(&capture_config, &capturer);
    }

    // Render: decoded far-end audio into the speaker ring, at the engine rate.
    esp_codec_dev_cfg_t output_config{};
    output_config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    output_config.data_if = &speaker_port_.base;
    esp_codec_dev_handle_t output = esp_codec_dev_new(&output_config);
    i2s_render_cfg_t render_config{};
    render_config.play_handle = output;
    audio_render_handle_t audio_render = output != nullptr ? av_render_alloc_i2s_render(&render_config) : nullptr;
    av_render_cfg_t av_config{};
    av_config.audio_render = audio_render;
    av_config.audio_raw_fifo_size = 8 * 4096;
    av_config.audio_render_fifo_size = 32 * 1024;
    av_config.allow_drop_data = true;
    av_render_handle_t renderer = audio_render != nullptr ? av_render_open(&av_config) : nullptr;
    if (renderer != nullptr) {
        av_render_audio_frame_info_t frame{};
        frame.sample_rate = sample_rate_;
        frame.channel = 1U;
        frame.bits_per_sample = 16U;
        (void)av_render_set_fixed_frame_info(renderer, &frame);
    }

    if (capturer == nullptr || renderer == nullptr) {
        ESP_LOGE(kTag, "media pipeline failed: capture=%p render=%p", capturer, renderer);
        return false;
    }
    capturer_ = capturer;
    renderer_ = renderer;
    return true;
}

bool LiveKitCall::StartAudio() {
    (void)xStreamBufferReset(speaker_ring_);
    (void)xStreamBufferReset(microphone_ring_);
    if (microphone_.StartWithReference(OnMicrophonePairs, this, 0xFFU) != MICROPIXEL_STATUS_OK) {
        ESP_LOGE(kTag, "microphone busy or missing");
        return false;
    }
    microphone_running_ = true;
    if (audio_.StartPcm(device::PcmSource{ReadSpeaker, this}, kSpeakerToken, 1000U, speaker_stream_) !=
        MICROPIXEL_STATUS_OK) {
        ESP_LOGE(kTag, "speaker stream failed");
        StopAudio();
        return false;
    }
    return true;
}

void LiveKitCall::StopAudio() {
    if (speaker_stream_ != 0U) {
        (void)audio_.StopPcm(speaker_stream_);
        speaker_stream_ = 0U;
    }
    if (microphone_running_) {
        (void)microphone_.Stop();
        microphone_running_ = false;
    }
}

bool LiveKitCall::RequestRoom() {
    char body[64];
    std::snprintf(body, sizeof(body), "{\"to\":\"%s\"}", callee_);
    char* response = nullptr;
    const int status = PostJson("/api/call", body, &response);
    cJSON* root = status == 200 && response != nullptr ? cJSON_Parse(response) : nullptr;
    const bool ok = root != nullptr && CopyField(root, "callId", call_id_, sizeof(call_id_)) &&
                    CopyField(root, "url", url_, sizeof(url_)) && CopyField(root, "token", token_, sizeof(token_)) &&
                    CopyField(root, "identity", identity_, sizeof(identity_));
    if (!ok) {
        ESP_LOGE(kTag, "call server: HTTP %d %s", status, response != nullptr ? response : "");
        call_id_[0] = '\0';
    }
    cJSON_Delete(root);
    heap_caps_free(response);
    return ok;
}

void LiveKitCall::NotifyServerEnded() {
    if (call_id_[0] == '\0') {
        return;
    }
    char body[96];
    std::snprintf(body, sizeof(body), "{\"callId\":\"%s\"}", call_id_);
    call_id_[0] = '\0';
    const int status = PostJson("/api/call/end", body, nullptr);
    if (status != 200) {
        ESP_LOGW(kTag, "call server end: HTTP %d", status);
    }
}

void LiveKitCall::OnMicrophonePairs(void* context, const int16_t* pairs, uint32_t count) {
    auto& self = *static_cast<LiveKitCall*>(context);
    // A full ring drops the newest audio; the capture reader is behind anyway.
    (void)xStreamBufferSend(self.microphone_ring_, pairs, count * 2U * sizeof(int16_t), 0U);
}

int LiveKitCall::ReadMicrophone(uint8_t* data, int size) {
    int filled = 0;
    while (filled < size) {
        const size_t got = xStreamBufferReceive(microphone_ring_, data + filled, size - filled, kMicrophoneWait);
        if (got == 0U) {
            // No clock (speaker stopped) or the call ended: hand back silence.
            std::memset(data + filled, 0, size - filled);
            break;
        }
        filled += static_cast<int>(got);
    }
    return ESP_CODEC_DEV_OK;
}

int LiveKitCall::WriteSpeaker(const uint8_t* data, int size) {
    (void)xStreamBufferSend(speaker_ring_, data, size, 0U);
    return ESP_CODEC_DEV_OK;
}

device::PcmReadResult LiveKitCall::ReadSpeaker(void* context, int16_t* mono_samples, uint32_t frame_capacity) {
    auto& self = *static_cast<LiveKitCall*>(context);
    const size_t wanted = frame_capacity * sizeof(int16_t);
    const size_t got = xStreamBufferReceive(self.speaker_ring_, mono_samples, wanted, 0U);
    if (got < wanted) {
        std::memset(reinterpret_cast<uint8_t*>(mono_samples) + got, 0, wanted - got);
    }
    // Never finished: the stream runs until the call stops it.
    return device::PcmReadResult{frame_capacity, false};
}

}  // namespace micropixel::platform::call
