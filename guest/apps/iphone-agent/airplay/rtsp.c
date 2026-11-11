/*
 * AirPlay RTSP/HTTP connections: request framing, header parsing and responses.
 * Ported from UxPlay lib/raop.c and lib/httpd.c. Copyright (C) 2011-2012 Juho Vähä-Herttua,
 * (C) 2021-2025 F. Duncanh; ESP32 port 2026 (ESP32 iPhone Agent); MicroPixel port 2026:
 * the App owns the sockets and pushes each connection's bytes in (ap_conn_data).
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ap_internal.h"

#define MAX_CONNS 3
#define MAX_REQUEST (64 * 1024)

airplay_cfg ap_cfg;
static ap_conn conns[MAX_CONNS];

void ap_log(const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  ap_host_log(msg);
}

const char *ap_header(const ap_req *r, const char *name) {
  for (int i = 0; i < r->nhdr; i++)
    if (!strcasecmp(r->names[i], name)) return r->vals[i];
  return NULL;
}

void ap_resp_header(ap_resp *r, const char *name, const char *val) {
  int n = snprintf(r->hdrs + r->hlen, sizeof r->hdrs - r->hlen, "%s: %s\r\n", name, val);
  if (n > 0 && r->hlen + n < sizeof r->hdrs) r->hlen += n;
}

int airplay_state(void) {
  if (ap_mirror_streaming()) return AIRPLAY_STREAMING;
  for (int i = 0; i < MAX_CONNS; i++)
    if (conns[i].sock && (conns[i].have_keymsg || ap_mirror_owner == &conns[i])) return AIRPLAY_CONNECTED;
  return AIRPLAY_IDLE;
}

static const char *find_bytes(const char *p, size_t n, const char *needle, size_t m) {
  for (size_t i = 0; i + m <= n; i++)
    if (!memcmp(p + i, needle, m)) return p + i;
  return NULL;
}

static const char *find_text_ci(const char *s, const char *needle) {
  size_t m = strlen(needle);
  for (; *s; s++)
    if (!strncasecmp(s, needle, m)) return s;
  return NULL;
}

ap_conn *ap_conn_open(uint32_t sock, uint32_t remote_ip) {
  for (int i = 0; i < MAX_CONNS; i++) {
    if (conns[i].sock) continue;
    memset(&conns[i], 0, sizeof conns[i]);
    conns[i].sock = sock;
    conns[i].remote_ip = remote_ip;
    ap_log("rtsp: connection from %u.%u.%u.%u", remote_ip & 0xff, (remote_ip >> 8) & 0xff,
           (remote_ip >> 16) & 0xff, remote_ip >> 24);
    return &conns[i];
  }
  return NULL;
}

void ap_conn_close(ap_conn *c) {
  if (!c->sock) return;
  if (ap_mirror_owner == c) {
    ap_mirror_stop();
    ap_mirror_owner = NULL;
  }
  ap_socket_close(c->sock);
  free(c->buf);
  memset(c, 0, sizeof *c);
  ap_log("rtsp: connection closed");
}

// Splits "Name: value" header lines; returns false on a malformed request line.
static bool parse_head(char *head, ap_req *r) {
  char *line = head, *next;
  memset(r, 0, sizeof *r);
  next = strstr(line, "\r\n");
  if (next) *next = 0;
  r->method = strtok_r(line, " ", &line);
  r->url = strtok_r(NULL, " ", &line);
  r->proto = strtok_r(NULL, " ", &line);
  if (!r->method || !r->url || !r->proto) return false;
  for (line = next ? next + 2 : NULL; line && *line; line = next ? next + 2 : NULL) {
    next = strstr(line, "\r\n");
    if (next) *next = 0;
    char *colon = strchr(line, ':');
    if (!colon || r->nhdr == AP_MAX_HDRS) continue;
    *colon++ = 0;
    while (*colon == ' ') colon++;
    r->names[r->nhdr] = line;
    r->vals[r->nhdr++] = colon;
  }
  return true;
}

static bool respond(ap_conn *c, const ap_req *req, ap_resp *res) {
  char head[640];
  int n = snprintf(head, sizeof head, "%s %d %s\r\n%.*sContent-Length: %u\r\n\r\n", req->proto, res->code,
                   res->reason, (int)res->hlen, res->hdrs, (unsigned)res->body_len);
  bool ok = n > 0 && n < (int)sizeof head && ap_send(c->sock, head, (size_t)n) &&
            (!res->body_len || ap_send(c->sock, res->body, res->body_len));
  free(res->body);
  return ok && !res->close;
}

// Handles every complete request in the connection buffer. False = close the connection.
static bool process(ap_conn *c) {
  for (;;) {
    char *end = c->len >= 4 ? (char *)find_bytes((const char *)c->buf, c->len, "\r\n\r\n", 4) : NULL;
    if (!end) return c->len < MAX_REQUEST;
    size_t head_len = end - (char *)c->buf + 4;
    *end = 0;  // the head becomes a C string; body follows at head_len
    const char *cl = find_text_ci((char *)c->buf, "\r\nContent-Length:");
    size_t body_len = cl ? strtoul(cl + 17, NULL, 10) : 0;
    if (head_len + body_len > MAX_REQUEST) return false;
    if (c->len < head_len + body_len) {
      *end = '\r';  // wait for the rest of the body
      return true;
    }
    ap_req req;
    ap_resp res = {.code = 200, .reason = "OK"};
    if (!parse_head((char *)c->buf, &req)) return false;
    req.body = c->buf + head_len;
    req.body_len = body_len;
    ap_handle(c, &req, &res);
    if (!respond(c, &req, &res)) return false;
    memmove(c->buf, c->buf + head_len + body_len, c->len - head_len - body_len);
    c->len -= head_len + body_len;
  }
}

bool ap_conn_data(ap_conn *c, const uint8_t *p, size_t n) {
  if (c->cap - c->len < n) {
    size_t cap = c->cap ? c->cap : 4096;
    while (cap - c->len < n) cap *= 2;
    if (cap > MAX_REQUEST + 4096) return false;
    uint8_t *b = realloc(c->buf, cap + 1);
    if (!b) return false;
    c->buf = b;
    c->cap = cap;
  }
  memcpy(c->buf + c->len, p, n);
  c->len += n;
  return process(c);
}

void airplay_set_display(uint16_t width, uint16_t height, uint8_t max_fps) {
  ap_cfg.width = width;
  ap_cfg.height = height;
  ap_cfg.max_fps = max_fps;
}

bool airplay_start(const airplay_cfg *cfg) {
  ap_cfg = *cfg;
  memset(conns, 0, sizeof conns);
  return ap_crypto_init(cfg->ed25519_seed);
}
