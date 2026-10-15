// The call flow: press the middle key to ring the phone, press again to hang up.
//
//   IDLE --press--> DIALING (ask call server) --> RINGING (in the room, waiting)
//   RINGING --phone joins--> IN_CALL --press / phone leaves--> ENDED --> IDLE

#include "call.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "livekit.h"

#include "board.h"
#include "call_server.h"
#include "display.h"
#include "media.h"

static const char *TAG = "call";

#define RING_TIMEOUT_MS   45000
#define ENDED_SHOW_MS     2500
#define LONG_PRESS_MS     2000
#define VOLUME_SHOW_MS    1500
#define VOLUME_STEP       10

typedef enum {
    EVT_PRESS,
    EVT_VOLUME_UP,
    EVT_VOLUME_DOWN,
    EVT_ROOM_STATE,
    EVT_REMOTE_JOINED,
    EVT_REMOTE_LEFT,
} event_type_t;

typedef struct {
    event_type_t type;
    int value;
} event_t;

typedef enum {
    STATE_IDLE,
    STATE_DIALING,
    STATE_RINGING,
    STATE_IN_CALL,
    STATE_ENDED,
} call_state_t;

static QueueHandle_t events;
static livekit_room_handle_t room;
static bool room_connected;  // Reached CONNECTED at least once this call.
static call_info_t call;
static call_state_t state = STATE_IDLE;
static int64_t state_since_ms;
static int64_t volume_shown_until_ms;
static int volume = CONFIG_GOTCHI_CALL_SPEAKER_VOLUME;
static char ended_message[32];
static int pulse;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void post(event_type_t type, int value)
{
    event_t evt = {.type = type, .value = value};
    xQueueSend(events, &evt, 0);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

static void draw(void)
{
    char line[32];
    display_fill(COLOR_BLACK);
    display_text_centered(8, "CGotchi", DISPLAY_FONT_SMALL, COLOR_ORANGE);

    switch (state) {
    case STATE_IDLE:
        display_fill_circle(DISPLAY_WIDTH / 2, 110, 62, COLOR_GREEN);
        display_text_centered(88, "CALL", DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(200, "Press the middle button", DISPLAY_FONT_SMALL, COLOR_GREY);
        break;
    case STATE_DIALING:
        display_fill_circle(DISPLAY_WIDTH / 2, 110, 56, COLOR_ORANGE);
        display_text_centered(88, "...", DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(200, "Connecting", DISPLAY_FONT_SMALL, COLOR_GREY);
        break;
    case STATE_RINGING:
        display_fill_circle(DISPLAY_WIDTH / 2, 110, (pulse & 1) ? 62 : 52, COLOR_ORANGE);
        display_text_centered(88, "Ring", DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(185, "Calling your iPhone", DISPLAY_FONT_SMALL, COLOR_WHITE);
        display_text_centered(212, "Press to cancel", DISPLAY_FONT_SMALL, COLOR_GREY);
        break;
    case STATE_IN_CALL: {
        int seconds = (int)((now_ms() - state_since_ms) / 1000);
        snprintf(line, sizeof(line), "%02d:%02d", seconds / 60, seconds % 60);
        display_fill_circle(DISPLAY_WIDTH / 2, 110, 62, COLOR_GREEN);
        display_text_centered(88, line, DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(212, "Press to hang up", DISPLAY_FONT_SMALL, COLOR_GREY);
        break;
    }
    case STATE_ENDED:
        display_text_centered(98, ended_message, DISPLAY_FONT_LARGE, COLOR_WHITE);
        break;
    }

    if (now_ms() < volume_shown_until_ms) {
        snprintf(line, sizeof(line), "Volume %d", volume);
        display_text(8, 212, line, DISPLAY_FONT_SMALL, COLOR_ORANGE);
    }
    display_flush();
}

static void set_state(call_state_t next)
{
    state = next;
    state_since_ms = now_ms();
    draw();
}

// ---------------------------------------------------------------------------
// Room
// ---------------------------------------------------------------------------

static void on_state_changed(livekit_connection_state_t room_state, void *ctx)
{
    ESP_LOGI(TAG, "room: %s", livekit_connection_state_str(room_state));
    post(EVT_ROOM_STATE, room_state);
}

static void on_participant_info(const livekit_participant_info_t *info, void *ctx)
{
    // The payload is freed after this returns, so only compare here.
    if (info->identity == NULL || strcmp(info->identity, call.identity) == 0) {
        return;
    }
    ESP_LOGI(TAG, "participant %s: state %d", info->identity, info->state);
    if (info->state == LIVEKIT_PARTICIPANT_STATE_ACTIVE) {
        post(EVT_REMOTE_JOINED, 0);
    } else if (info->state == LIVEKIT_PARTICIPANT_STATE_DISCONNECTED) {
        post(EVT_REMOTE_LEFT, 0);
    }
}

static bool join_room(void)
{
    room_connected = false;
    livekit_room_options_t options = {
        .publish = {
            .kind = LIVEKIT_MEDIA_TYPE_AUDIO,
            .audio_encode = {
                .codec = LIVEKIT_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel_count = 1,
            },
            .capturer = media_get_capturer(),
        },
        .subscribe = {
            .kind = LIVEKIT_MEDIA_TYPE_AUDIO,
            .renderer = media_get_renderer(),
        },
        .on_state_changed = on_state_changed,
        .on_participant_info = on_participant_info,
    };
    if (livekit_room_create(&room, &options) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(TAG, "room create failed");
        room = NULL;
        return false;
    }
    if (livekit_room_connect(room, call.url, call.token) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(TAG, "room connect failed");
        livekit_room_destroy(room);
        room = NULL;
        return false;
    }
    return true;
}

static void hang_up(const char *message)
{
    if (room != NULL) {
        livekit_room_close(room);
        livekit_room_destroy(room);
        room = NULL;
    }
    call_server_end(call.call_id);
    snprintf(ended_message, sizeof(ended_message), "%s", message);
    set_state(STATE_ENDED);
}

static void dial(void)
{
    set_state(STATE_DIALING);
    memset(&call, 0, sizeof(call));
    if (!call_server_start(&call)) {
        snprintf(ended_message, sizeof(ended_message), "Can't call");
        set_state(STATE_ENDED);
        return;
    }
    if (!join_room()) {
        hang_up("Can't call");
        return;
    }
    set_state(STATE_RINGING);
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------

static void handle(const event_t *evt)
{
    switch (evt->type) {
    case EVT_VOLUME_UP:
    case EVT_VOLUME_DOWN:
        volume += evt->type == EVT_VOLUME_UP ? VOLUME_STEP : -VOLUME_STEP;
        volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
        media_set_volume(volume);
        volume_shown_until_ms = now_ms() + VOLUME_SHOW_MS;
        draw();
        return;
    case EVT_PRESS:
        if (state == STATE_IDLE) {
            dial();
        } else if (state == STATE_RINGING) {
            hang_up("Cancelled");
        } else if (state == STATE_IN_CALL) {
            hang_up("Call ended");
        }
        return;
    case EVT_REMOTE_JOINED:
        if (state == STATE_RINGING) {
            set_state(STATE_IN_CALL);
        }
        return;
    case EVT_REMOTE_LEFT:
        if (state == STATE_IN_CALL || state == STATE_RINGING) {
            hang_up(state == STATE_IN_CALL ? "Call ended" : "Declined");
        }
        return;
    case EVT_ROOM_STATE:
        if (evt->value == LIVEKIT_CONNECTION_STATE_CONNECTED) {
            room_connected = true;
        }
        // The server deletes the room when the phone declines or hangs up.
        if ((state == STATE_RINGING || state == STATE_IN_CALL) &&
            (evt->value == LIVEKIT_CONNECTION_STATE_FAILED ||
             (evt->value == LIVEKIT_CONNECTION_STATE_DISCONNECTED && room_connected))) {
            hang_up(state == STATE_IN_CALL ? "Call ended" : "No answer");
        }
        return;
    }
}

static void tick(void)
{
    int64_t elapsed = now_ms() - state_since_ms;
    if (state == STATE_RINGING && elapsed > RING_TIMEOUT_MS) {
        hang_up("No answer");
    } else if (state == STATE_ENDED && elapsed > ENDED_SHOW_MS) {
        set_state(STATE_IDLE);
    } else if (state == STATE_RINGING || state == STATE_IN_CALL || volume_shown_until_ms != 0) {
        if (now_ms() >= volume_shown_until_ms) {
            volume_shown_until_ms = 0;
        }
        pulse++;
        draw();
    }
}

static void call_task(void *arg)
{
    set_state(STATE_IDLE);
    event_t evt;
    while (true) {
        if (xQueueReceive(events, &evt, pdMS_TO_TICKS(500)) == pdTRUE) {
            handle(&evt);
        } else {
            tick();
        }
    }
}

// Polls the three keys every 20 ms. A long hold on the middle key powers off.
static void button_task(void *arg)
{
    bool middle_was = false, up_was = false, down_was = false;
    int64_t middle_since = 0;
    bool middle_long = false;
    while (true) {
        bool middle = gpio_get_level(BOARD_BUTTON_MIDDLE) == 1;
        bool up = gpio_get_level(BOARD_BUTTON_VOL_UP) == 0;
        bool down = gpio_get_level(BOARD_BUTTON_VOL_DOWN) == 0;

        if (middle && !middle_was) {
            middle_since = now_ms();
            middle_long = false;
        } else if (middle && !middle_long && now_ms() - middle_since > LONG_PRESS_MS) {
            middle_long = true;
            ESP_LOGI(TAG, "power off");
            display_fill(COLOR_BLACK);
            display_text_centered(98, "Bye", DISPLAY_FONT_LARGE, COLOR_WHITE);
            display_flush();
            vTaskDelay(pdMS_TO_TICKS(500));
            board_power_off();
        } else if (!middle && middle_was && !middle_long) {
            post(EVT_PRESS, 0);
        }
        if (up && !up_was) {
            post(EVT_VOLUME_UP, 0);
        }
        if (down && !down_was) {
            post(EVT_VOLUME_DOWN, 0);
        }
        middle_was = middle;
        up_was = up;
        down_was = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void call_start(void)
{
    events = xQueueCreate(16, sizeof(event_t));
    xTaskCreate(call_task, "call", 8192, NULL, 5, NULL);
    xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL);
}
