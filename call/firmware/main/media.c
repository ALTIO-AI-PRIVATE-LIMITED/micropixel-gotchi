// Capture (mics -> Opus) and render (Opus -> speaker) pipelines for LiveKit.
// Same as LiveKit's custom_hardware example, with the Gotchi's AEC reference
// on ES7210 TDM slot 2 instead of slot 1.

#include "esp_check.h"
#include "esp_log.h"
#include "av_render_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"

#include "board.h"
#include "media.h"

static const char *TAG = "media";

#define NULL_CHECK(pointer, message) \
    ESP_RETURN_ON_FALSE(pointer != NULL, -1, TAG, message)

static esp_capture_handle_t  capturer;
static av_render_handle_t    renderer;
static esp_codec_dev_handle_t playback;

static int build_capturer(void)
{
    esp_codec_dev_handle_t record = board_get_record_handle();
    NULL_CHECK(record, "no record handle");

    // Four TDM slots; use slot 0 (MIC1) as the mic and slot 2 (speaker
    // loopback) as the echo-cancellation reference.
    esp_capture_audio_aec_src_cfg_t aec_cfg = {
        .record_handle = record,
        .channel = 4,
        .channel_mask = (1 << 0) | (1 << 2),
    };
    esp_capture_audio_src_if_t *source = esp_capture_new_audio_aec_src(&aec_cfg);
    NULL_CHECK(source, "no AEC source");

    esp_capture_cfg_t cfg = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_AUDIO,
        .audio_src = source,
    };
    esp_capture_open(&cfg, &capturer);
    NULL_CHECK(capturer, "capture open failed");
    return 0;
}

static int build_renderer(void)
{
    playback = board_get_playback_handle();
    NULL_CHECK(playback, "no playback handle");

    i2s_render_cfg_t i2s_cfg = {
        .play_handle = playback,
    };
    audio_render_handle_t audio_render = av_render_alloc_i2s_render(&i2s_cfg);
    NULL_CHECK(audio_render, "I2S renderer failed");

    media_set_volume(CONFIG_GOTCHI_CALL_SPEAKER_VOLUME);

    av_render_cfg_t render_cfg = {
        .audio_render = audio_render,
        .audio_raw_fifo_size = 8 * 4096,
        .audio_render_fifo_size = 100 * 1024,
        .allow_drop_data = false,
    };
    renderer = av_render_open(&render_cfg);
    NULL_CHECK(renderer, "AV renderer failed");

    av_render_audio_frame_info_t frame_info = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };
    av_render_set_fixed_frame_info(renderer, &frame_info);
    return 0;
}

int media_init(void)
{
    esp_audio_enc_register_default();
    esp_audio_dec_register_default();
    if (build_capturer() != 0 || build_renderer() != 0) {
        return -1;
    }
    return 0;
}

esp_capture_handle_t media_get_capturer(void)
{
    return capturer;
}

av_render_handle_t media_get_renderer(void)
{
    return renderer;
}

void media_set_volume(int percent)
{
    esp_codec_dev_set_out_vol(playback, percent);
}
