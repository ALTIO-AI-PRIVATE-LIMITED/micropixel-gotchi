// Google Meet on the Gotchi, audio only.
//
//   LIST (today's meetings) --start time / middle key--> CONNECTING (server
//   sends the bot) --> WAITING (in the room, bot not in the meeting yet)
//   --bot joins--> IN_CALL --middle key / meeting ends--> ENDED --> LIST
//
// Calls start muted; Vol+ toggles the mic. Vol- steps the speaker volume.

#include "meet.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "livekit.h"

#include "board.h"
#include "display.h"
#include "media.h"
#include "meet_server.h"

static const char *TAG = "meet";

#define REFRESH_MS        60000
#define WAIT_TIMEOUT_MS   (10 * 60 * 1000)
#define ENDED_SHOW_MS     2500
#define LONG_PRESS_MS     2000
#define VOLUME_SHOW_MS    1500
#define VOLUME_STEP       20
#define LIST_ROWS         3
#define HANDLED_MAX       8

typedef enum {
    EVT_PRESS,
    EVT_VOL_UP,
    EVT_VOL_DOWN,
    EVT_MEETINGS,
    EVT_ROOM_STATE,
    EVT_REMOTE_JOINED,
    EVT_REMOTE_LEFT,
} event_type_t;

typedef struct {
    event_type_t type;
    int value;
} event_t;

typedef enum {
    STATE_LIST,
    STATE_CONNECTING,
    STATE_WAITING,
    STATE_IN_CALL,
    STATE_ENDED,
} meet_state_t;

static QueueHandle_t events;
static TaskHandle_t fetch_task_handle;
static volatile meet_state_t state = STATE_LIST;
static int64_t state_since_ms;

// Written by the fetch task, copied into `meetings` on EVT_MEETINGS.
static SemaphoreHandle_t fetched_lock;
static meeting_t fetched[MEET_MAX_MEETINGS];
static int fetched_count = -1;  // -1: never fetched or last fetch failed.

static meeting_t meetings[MEET_MAX_MEETINGS];
static int meeting_count = -1;  // -1 until the first successful fetch.
static bool fetch_failed;
static int selected;

// Meetings already joined (or tried), so auto-join doesn't rejoin after you leave.
static char handled[HANDLED_MAX][sizeof(((meeting_t *)0)->id)];
static int handled_next;

static livekit_room_handle_t room;
static bool room_connected;  // Reached CONNECTED at least once this call.
static meet_session_t session;
static char call_title[sizeof(((meeting_t *)0)->title)];
static bool muted;
static int volume = CONFIG_GOTCHI_CALL_SPEAKER_VOLUME;
static int64_t volume_shown_until_ms;
static char ended_message[32];
static int pulse;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool clock_valid(void)
{
    return time(NULL) > 1700000000;  // SNTP has set the clock.
}

static void post(event_type_t type, int value)
{
    event_t evt = {.type = type, .value = value};
    xQueueSend(events, &evt, 0);
}

static bool is_handled(const char *id)
{
    for (int i = 0; i < HANDLED_MAX; i++) {
        if (strcmp(handled[i], id) == 0) {
            return true;
        }
    }
    return false;
}

static void mark_handled(const char *id)
{
    if (!is_handled(id)) {
        snprintf(handled[handled_next], sizeof(handled[0]), "%s", id);
        handled_next = (handled_next + 1) % HANDLED_MAX;
    }
}

static void set_muted(bool mute)
{
    muted = mute;
    esp_codec_dev_set_in_mute(board_get_record_handle(), mute);
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

// Copies `text` into `out`, cut with ".." to fit `max_width` pixels.
static void fit(const char *text, display_font_t font, int max_width, char *out, size_t size)
{
    snprintf(out, size, "%s", text);
    size_t len = strlen(out);
    if (len > size - 3) {
        len = size - 3;
    }
    while (len > 0 && display_text_width(out, font) > max_width) {
        len--;
        memcpy(out + len, "..", 3);
    }
}

static void draw_list(void)
{
    char line[80];
    display_text_centered(8, "Meetings", DISPLAY_FONT_SMALL, COLOR_ORANGE);

    if (meeting_count <= 0) {
        const char *message = meeting_count == 0 ? "No meetings" : (fetch_failed ? "Offline" : "Loading...");
        display_text_centered(98, message, DISPLAY_FONT_LARGE, COLOR_WHITE);
        if (meeting_count == 0) {
            display_text_centered(150, "in the next 24 hours", DISPLAY_FONT_SMALL, COLOR_GREY);
        }
        return;
    }

    time_t t = time(NULL);
    int first = selected - LIST_ROWS + 1 > 0 ? selected - LIST_ROWS + 1 : 0;
    for (int row = 0; row < LIST_ROWS && first + row < meeting_count; row++) {
        const meeting_t *m = &meetings[first + row];
        bool live = clock_valid() && m->start <= t && t < m->end;
        bool is_selected = first + row == selected;
        int y = 40 + row * 52;
        display_text(24, y, live ? "Now" : m->label, DISPLAY_FONT_SMALL, live ? COLOR_GREEN : COLOR_ORANGE);
        fit(m->title, DISPLAY_FONT_SMALL, DISPLAY_WIDTH - 32, line, sizeof(line));
        display_text(24, y + 22, line, DISPLAY_FONT_SMALL, is_selected ? COLOR_WHITE : COLOR_GREY);
        if (is_selected) {
            display_text(6, y + 22, ">", DISPLAY_FONT_SMALL, COLOR_WHITE);
        }
    }
    display_text_centered(212, "Middle key joins", DISPLAY_FONT_SMALL, COLOR_GREY);
}

static void draw_call(void)
{
    char line[80];
    fit(call_title, DISPLAY_FONT_SMALL, DISPLAY_WIDTH - 16, line, sizeof(line));
    display_text_centered(8, line, DISPLAY_FONT_SMALL, COLOR_WHITE);

    if (state == STATE_IN_CALL) {
        int seconds = (int)((now_ms() - state_since_ms) / 1000);
        snprintf(line, sizeof(line), "%02d:%02d", seconds / 60, seconds % 60);
        display_fill_circle(DISPLAY_WIDTH / 2, 110, 62, muted ? COLOR_RED : COLOR_GREEN);
        display_text_centered(88, line, DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(185, muted ? "Muted: Vol+ to talk" : "Live: Vol+ to mute",
                              DISPLAY_FONT_SMALL, COLOR_WHITE);
    } else {
        display_fill_circle(DISPLAY_WIDTH / 2, 110, (pulse & 1) ? 62 : 52, COLOR_ORANGE);
        display_text_centered(88, "...", DISPLAY_FONT_LARGE, COLOR_WHITE);
        display_text_centered(185, state == STATE_CONNECTING ? "Joining" : "Waiting to be let in",
                              DISPLAY_FONT_SMALL, COLOR_WHITE);
    }
    display_text_centered(212, "Middle key leaves", DISPLAY_FONT_SMALL, COLOR_GREY);
}

static void draw(void)
{
    display_fill(COLOR_BLACK);
    switch (state) {
    case STATE_LIST:
        draw_list();
        break;
    case STATE_CONNECTING:
    case STATE_WAITING:
    case STATE_IN_CALL:
        draw_call();
        break;
    case STATE_ENDED:
        display_text_centered(98, ended_message, DISPLAY_FONT_LARGE, COLOR_WHITE);
        break;
    }

    if (now_ms() < volume_shown_until_ms) {
        char line[32];
        snprintf(line, sizeof(line), "Volume %d", volume);
        display_fill(COLOR_BLACK);  // Keep it simple: volume gets the whole screen briefly.
        display_text_centered(98, line, DISPLAY_FONT_LARGE, COLOR_ORANGE);
    }
    display_flush();
}

static void set_state(meet_state_t next)
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
    if (info->identity == NULL || strcmp(info->identity, session.identity) == 0) {
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
    if (livekit_room_connect(room, session.url, session.token) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(TAG, "room connect failed");
        livekit_room_destroy(room);
        room = NULL;
        return false;
    }
    return true;
}

static void leave(const char *message)
{
    if (room != NULL) {
        livekit_room_close(room);
        livekit_room_destroy(room);
        room = NULL;
    }
    meet_server_leave(&session);
    set_muted(true);
    snprintf(ended_message, sizeof(ended_message), "%s", message);
    set_state(STATE_ENDED);
}

static void join(const meeting_t *m)
{
    ESP_LOGI(TAG, "joining %s", m->title);
    mark_handled(m->id);
    snprintf(call_title, sizeof(call_title), "%s", m->title);
    memset(&session, 0, sizeof(session));
    set_muted(true);
    set_state(STATE_CONNECTING);
    if (!meet_server_join(m->id, &session)) {
        snprintf(ended_message, sizeof(ended_message), "Can't join");
        set_state(STATE_ENDED);
        return;
    }
    if (!join_room()) {
        leave("Can't join");
        return;
    }
    set_state(STATE_WAITING);
}

// Joins the first meeting that has started and wasn't joined yet.
static void auto_join(void)
{
    if (!clock_valid()) {
        return;
    }
    time_t t = time(NULL);
    for (int i = 0; i < meeting_count; i++) {
        if (meetings[i].start <= t && t < meetings[i].end && !is_handled(meetings[i].id)) {
            selected = i;
            join(&meetings[i]);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------

static bool in_call(void)
{
    return state == STATE_WAITING || state == STATE_IN_CALL;
}

static void handle(const event_t *evt)
{
    switch (evt->type) {
    case EVT_MEETINGS:
        xSemaphoreTake(fetched_lock, portMAX_DELAY);
        fetch_failed = fetched_count < 0;
        if (!fetch_failed) {
            memcpy(meetings, fetched, sizeof(meetings));
            meeting_count = fetched_count;
        }
        xSemaphoreGive(fetched_lock);
        if (selected >= meeting_count) {
            selected = meeting_count > 0 ? meeting_count - 1 : 0;
        }
        if (state == STATE_LIST) {
            draw();
        }
        return;
    case EVT_VOL_UP:
        if (in_call()) {
            set_muted(!muted);
            ESP_LOGI(TAG, "mic %s", muted ? "muted" : "live");
        } else if (state == STATE_LIST && selected > 0) {
            selected--;
        }
        draw();
        return;
    case EVT_VOL_DOWN:
        if (in_call()) {
            volume = volume - VOLUME_STEP < VOLUME_STEP ? 100 : volume - VOLUME_STEP;
            media_set_volume(volume);
            volume_shown_until_ms = now_ms() + VOLUME_SHOW_MS;
        } else if (state == STATE_LIST && selected < meeting_count - 1) {
            selected++;
        }
        draw();
        return;
    case EVT_PRESS:
        if (state == STATE_LIST && meeting_count > 0) {
            join(&meetings[selected]);
        } else if (in_call()) {
            leave("Left meeting");
        }
        return;
    case EVT_REMOTE_JOINED:
        if (state == STATE_WAITING) {
            set_state(STATE_IN_CALL);
        }
        return;
    case EVT_REMOTE_LEFT:
        // The bot's page closes when the bot leaves the meeting.
        if (in_call()) {
            leave("Meeting over");
        }
        return;
    case EVT_ROOM_STATE:
        if (evt->value == LIVEKIT_CONNECTION_STATE_CONNECTED) {
            room_connected = true;
        }
        if (in_call() &&
            (evt->value == LIVEKIT_CONNECTION_STATE_FAILED ||
             (evt->value == LIVEKIT_CONNECTION_STATE_DISCONNECTED && room_connected))) {
            leave("Disconnected");
        }
        return;
    }
}

static void tick(void)
{
    int64_t elapsed = now_ms() - state_since_ms;
    if (now_ms() >= volume_shown_until_ms) {
        volume_shown_until_ms = 0;
    }
    if (state == STATE_WAITING && elapsed > WAIT_TIMEOUT_MS) {
        leave("Not let in");
    } else if (state == STATE_ENDED && elapsed > ENDED_SHOW_MS) {
        set_state(STATE_LIST);
        xTaskNotifyGive(fetch_task_handle);  // Refresh the list now.
    } else if (state == STATE_LIST) {
        auto_join();
        if (state == STATE_LIST) {
            draw();
        }
    } else {
        pulse++;
        draw();
    }
}

static void meet_task(void *arg)
{
    set_muted(true);
    set_state(STATE_LIST);
    event_t evt;
    while (true) {
        if (xQueueReceive(events, &evt, pdMS_TO_TICKS(500)) == pdTRUE) {
            handle(&evt);
        } else {
            tick();
        }
    }
}

// Refreshes the meeting list every minute while the list is showing.
static void fetch_task(void *arg)
{
    static meeting_t buf[MEET_MAX_MEETINGS];
    while (true) {
        if (state == STATE_LIST) {
            memset(buf, 0, sizeof(buf));
            int count = meet_server_meetings(buf, MEET_MAX_MEETINGS);
            xSemaphoreTake(fetched_lock, portMAX_DELAY);
            if (count >= 0) {
                memcpy(fetched, buf, sizeof(fetched));
            }
            fetched_count = count;
            xSemaphoreGive(fetched_lock);
            post(EVT_MEETINGS, 0);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REFRESH_MS));
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
            post(EVT_VOL_UP, 0);
        }
        if (down && !down_was) {
            post(EVT_VOL_DOWN, 0);
        }
        middle_was = middle;
        up_was = up;
        down_was = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void meet_start(void)
{
    events = xQueueCreate(16, sizeof(event_t));
    fetched_lock = xSemaphoreCreateMutex();
    xTaskCreate(meet_task, "meet", 8192, NULL, 5, NULL);
    xTaskCreate(fetch_task, "fetch", 8192, NULL, 4, &fetch_task_handle);
    xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL);
}
