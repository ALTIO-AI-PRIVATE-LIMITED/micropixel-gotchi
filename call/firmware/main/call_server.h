#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char call_id[64];
    char url[128];
    char token[1024];
    char identity[64];  // The Gotchi's own identity in the room.
} call_info_t;

/// Ask the server for a room and ring the phone. Blocks for the HTTPS round trip.
bool call_server_start(call_info_t *info);

/// Tell the server the call is over, so the phone stops ringing and the room closes.
void call_server_end(const char *call_id);

#ifdef __cplusplus
}
#endif
