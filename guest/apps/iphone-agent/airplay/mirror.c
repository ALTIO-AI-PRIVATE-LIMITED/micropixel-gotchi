/*
 * AirPlay mirror data channel: stream of 128-byte headers + payloads, AES-128-CTR
 * decryption of the H.264 access units, AVCC -> Annex-B, SPS/PPS extraction; plus the
 * minimal NTP timing client.
 * Ported from UxPlay lib/raop_rtp_mirror.c, lib/mirror_buffer.c and lib/raop_ntp.c.
 * Copyright (C) 2019 dsafa22, (C) 2018 Juho Vähä-Herttua, (C) 2021-2025 F. Duncanh; ESP32
 * port 2026 (ESP32 iPhone Agent); MicroPixel port 2026: the App owns the sockets and
 * pushes the data connection's bytes in (ap_mirror_feed).
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../video/gop.h"
#include "ap_internal.h"

#define MAX_PAYLOAD (2 * 1024 * 1024)
#define NTP_INTERVAL_US 3000000

static struct {
  bool run;
  uint64_t last_data_us, next_ntp_us;
  ap_aes_ctr aes;
  uint32_t remote_ip;
  uint16_t timing_rport;
  uint8_t hdr[128], ntp[32];
  uint8_t *payload;
  size_t got, need, cap;
  bool in_payload;
} m;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// SPS + PPS from the unencrypted codec packet (an avcC record) -> Annex-B config.
static void handle_codec(const uint8_t *p, size_t n) {
  static const uint8_t sc[4] = {0, 0, 0, 1};
  uint8_t cfg[GOP_CFG_MAX];
  if (n >= 8 && !memcmp(p + 4, "hvc1", 4)) { ap_log("mirror: H.265 stream not supported"); return; }
  if (n < 11) return;
  size_t sps = p[6] << 8 | p[7];
  if (8 + sps + 3 > n) return;
  size_t pps = p[sps + 9] << 8 | p[sps + 10];
  if (11 + sps + pps > n || 8 + sps + pps > sizeof cfg) return;
  memcpy(cfg, sc, 4);
  memcpy(cfg + 4, p + 8, sps);
  memcpy(cfg + 4 + sps, sc, 4);
  memcpy(cfg + 8 + sps, p + sps + 11, pps);
  ap_log("mirror: SPS %u + PPS %u bytes", (unsigned)sps, (unsigned)pps);
  ap_host_on_config(cfg, 8 + sps + pps);
}

static void handle_packet(const uint8_t *hdr, uint8_t *p, size_t n) {
  m.last_data_us = ap_now_us();
  if (hdr[4] == 0x01) { handle_codec(p, n); return; }
  if (hdr[4] != 0x00 || !n) return;  // 0x02/0x05: heartbeat and streaming reports
  // the keystream continues across packets, as in mirror_buffer_decrypt()
  ap_aes_ctr_crypt(&m.aes, p, n);
  if (!avcc_to_annexb(p, n)) { ap_log("mirror: undecodable payload (%u bytes)", (unsigned)n); return; }
  ap_host_on_au(p, n, annexb_has_idr(p, n));
}

bool ap_mirror_feed(const uint8_t *data, size_t n) {
  while (m.run && n) {
    uint8_t *dst = m.in_payload ? m.payload + m.got : m.hdr + m.got;
    size_t k = m.need - m.got < n ? m.need - m.got : n;
    memcpy(dst, data, k);
    m.got += k;
    data += k;
    n -= k;
    if (m.got < m.need) return true;
    if (!m.in_payload) {
      size_t need = le32(m.hdr);
      if (need > MAX_PAYLOAD) { ap_log("mirror: oversized packet"); return false; }
      if (need > m.cap) {
        free(m.payload);
        m.cap = need < 256 * 1024 ? 256 * 1024 : need;
        m.payload = malloc(m.cap);
        if (!m.payload) { m.cap = 0; ap_log("mirror: out of memory"); return false; }
      }
      m.got = 0;
      m.need = need;
      m.in_payload = need > 0;
      if (m.in_payload) continue;
      handle_packet(m.hdr, m.payload, 0);
    } else {
      handle_packet(m.hdr, m.payload, m.need);
    }
    m.got = 0, m.need = 128, m.in_payload = false;
  }
  return true;
}

void ap_mirror_data_closed(void) {
  m.got = 0, m.need = 128, m.in_payload = false;
  ap_log("mirror: data connection closed");
}

void ap_mirror_ntp_reply(const uint8_t *p, size_t n) {
  if (n >= 32) memcpy(m.ntp + 8, p + 24, 8);  // echo the client's transmit time as reference
}

void ap_mirror_tick(void) {
  if (!m.run || !m.timing_rport) return;
  uint64_t us = ap_now_us();
  if (us < m.next_ntp_us) return;
  m.next_ntp_us = us + NTP_INTERVAL_US;
  uint32_t sec = (uint32_t)(us / 1000000) + 2208988800u, frac = (uint32_t)(((us % 1000000) << 32) / 1000000);
  for (int i = 0; i < 4; i++) {
    m.ntp[24 + i] = (uint8_t)(sec >> (24 - 8 * i));
    m.ntp[28 + i] = (uint8_t)(frac >> (24 - 8 * i));
  }
  ap_host_send_ntp(m.ntp, m.remote_ip, m.timing_rport);
}

static void derive(const char *label, uint64_t id, const uint8_t aeskey[16], uint8_t out[16]) {
  char s[64];
  uint8_t h[64];
  snprintf(s, sizeof s, "%s%" PRIu64, label, id);
  ap_sha512(s, strlen(s), aeskey, 16, h);
  memcpy(out, h, 16);
}

bool ap_mirror_start(const uint8_t aeskey[16], uint64_t stream_id, uint32_t remote_ip, uint16_t timing_rport) {
  uint8_t key[16], iv[16];
  ap_mirror_stop();
  derive("AirPlayStreamKey", stream_id, aeskey, key);
  derive("AirPlayStreamIV", stream_id, aeskey, iv);
  ap_aes_ctr_init(&m.aes, key, iv);
  m.remote_ip = remote_ip;
  m.timing_rport = timing_rport;
  m.last_data_us = 0;
  m.next_ntp_us = 0;
  memset(m.ntp, 0, sizeof m.ntp);
  m.ntp[0] = 0x80, m.ntp[1] = 0xd2, m.ntp[3] = 0x07;
  m.got = 0, m.need = 128, m.in_payload = false;
  if (!ap_host_mirror_open()) return false;
  m.run = true;
  return true;
}

void ap_mirror_stop(void) {
  if (!m.run) return;
  m.run = false;
  ap_host_mirror_close();
  free(m.payload);
  m.payload = NULL;
  m.cap = 0;
}

bool ap_mirror_running(void) { return m.run; }

bool ap_mirror_streaming(void) {
  return m.run && m.last_data_us && ap_now_us() - m.last_data_us < 3000000;
}
