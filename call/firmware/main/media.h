#pragma once

#include "esp_capture.h"
#include "av_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Build the capture (mic) and render (speaker) pipelines. Call after board_init().
int media_init(void);

esp_capture_handle_t media_get_capturer(void);
av_render_handle_t media_get_renderer(void);

/// Speaker volume, 0-100.
void media_set_volume(int percent);

#ifdef __cplusplus
}
#endif
