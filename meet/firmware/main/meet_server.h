#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEET_MAX_MEETINGS 8

typedef struct {
    char id[128];      // Calendar event id.
    char title[64];
    char label[16];    // Start time in the calendar's time zone, e.g. "10:30".
    int64_t start;     // Unix seconds.
    int64_t end;
} meeting_t;

typedef struct {
    char meet_id[64];
    char bot_id[64];
    char url[128];
    char token[1024];
    char identity[64];  // The Gotchi's own identity in the room.
} meet_session_t;

/// Fetch upcoming meetings. Returns the count (0..MEET_MAX_MEETINGS) or -1.
/// Blocks for the HTTPS round trip.
int meet_server_meetings(meeting_t *out, int max);

/// Send the bot into a meeting and get the Gotchi's room details.
bool meet_server_join(const char *event_id, meet_session_t *session);

/// Take the bot out of the meeting.
void meet_server_leave(const meet_session_t *session);

#ifdef __cplusplus
}
#endif
