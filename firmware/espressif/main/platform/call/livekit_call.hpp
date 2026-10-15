#pragma once

#include <atomic>
#include <cstdint>

#include "audio_codec_data_if.h"
#include "device/contracts/audio.hpp"
#include "device/contracts/call.hpp"
#include "device/contracts/microphone.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

namespace micropixel::platform::call {

// Voice calls over LiveKit (WebRTC). Dial() asks the call server
// (CONFIG_MICROPIXEL_CALL_SERVER_URL) for a room and a token; the server
// rings the phone. While a call runs, the far end plays through a Host PCM
// stream on the audio engine and the microphones (with the speaker loopback
// as echo reference) feed LiveKit's capture pipeline.
class LiveKitCall final : public device::Call {
   public:
    LiveKitCall(device::Audio& audio, device::Microphone& microphone) : audio_(audio), microphone_(microphone) {}

    [[nodiscard]] esp_err_t Initialize();

    [[nodiscard]] micropixel_call_state_t State() const override;
    [[nodiscard]] int32_t Dial(const char* callee, device::CallStateSink sink, void* context) override;
    [[nodiscard]] int32_t HangUp() override;
    void Release() override;

    // Bridges between esp_codec_dev and the rings below; public for the C callbacks.
    struct Port final {
        audio_codec_data_if_t base;
        LiveKitCall* owner;
    };
    int ReadMicrophone(uint8_t* data, int size);
    int WriteSpeaker(const uint8_t* data, int size);

   private:
    enum class Command : uint8_t { kDial, kHangUp, kRoomState, kRemoteJoined, kRemoteLeft };
    struct Message final {
        Command command;
        int32_t value;
    };

    static constexpr uint32_t kRingTimeoutMs = 45000U;
    static constexpr uint32_t kSpeakerRingBytes = 16000U * 2U;     // 1 s mono
    static constexpr uint32_t kMicrophoneRingBytes = 16000U * 4U;  // 1 s of (mic, reference) pairs

    static void TaskEntry(void* argument);
    void Run();
    void Handle(const Message& message);
    void StartCall();
    void EndCall(uint8_t reason);
    void SetPhase(uint8_t phase, uint8_t reason);
    [[nodiscard]] bool EnsureMedia();
    [[nodiscard]] bool StartAudio();
    void StopAudio();
    [[nodiscard]] bool RequestRoom();
    void NotifyServerEnded();
    void Post(Command command, int32_t value = 0);

    friend struct RoomCallbacks;
    static void OnMicrophonePairs(void* context, const int16_t* pairs, uint32_t count);
    static device::PcmReadResult ReadSpeaker(void* context, int16_t* mono_samples, uint32_t frame_capacity);

    device::Audio& audio_;
    device::Microphone& microphone_;
    TaskHandle_t task_{};
    QueueHandle_t queue_{};
    SemaphoreHandle_t sink_mutex_{};
    StreamBufferHandle_t speaker_ring_{};
    StreamBufferHandle_t microphone_ring_{};
    Port speaker_port_{};
    Port microphone_port_{};
    void* capturer_{};
    void* renderer_{};
    void* room_{};
    bool room_connected_{};
    device::PcmStreamHandle speaker_stream_{};
    bool microphone_running_{};
    uint32_t sample_rate_{16000U};

    // Written by the call task, read by any task.
    std::atomic<uint8_t> phase_{MICROPIXEL_CALL_PHASE_IDLE};
    std::atomic<uint8_t> end_reason_{MICROPIXEL_CALL_END_NONE};
    std::atomic<int64_t> phase_since_us_{};
    std::atomic<bool> hang_up_requested_{};
    // Guarded by sink_mutex_.
    device::CallStateSink sink_{};
    void* sink_context_{};

    // Call task only.
    char callee_[MICROPIXEL_CALL_MAX_CALLEE_BYTES + 1U]{};
    char call_id_[64]{};
    char url_[160]{};
    char token_[1024]{};
    char identity_[64]{};
};

}  // namespace micropixel::platform::call
