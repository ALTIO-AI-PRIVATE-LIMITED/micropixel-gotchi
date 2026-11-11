/*
 * Internal declarations of the AirPlay receiver, ported from UxPlay lib/ (raop.c,
 * raop_handlers.h, pairing.c, fairplay_playfair.c, raop_rtp_mirror.c, mirror_buffer.c,
 * raop_ntp.c, dnssd). Copyright (C) 2011-2018 Juho Vähä-Herttua, (C) 2020 Jaslo Ziska,
 * (C) 2021-2025 F. Duncanh and UxPlay contributors; ESP32 port 2026 (ESP32 iPhone Agent,
 * Henry Yuan); MicroPixel Guest port 2026: sockets, timers and randomness come from the
 * App through the host hooks at the end of this file.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#pragma once
#include "airplay.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "../vendor/tiny-aes/aes.h"

#define AP_RTSP_PORT 7000
#define AP_TIMING_PORT 7010
#define AP_AUDIO_DATA_PORT 7011
#define AP_AUDIO_CTRL_PORT 7012
#define AP_MIRROR_PORT 7100

#define AP_MODEL "AppleTV3,2"
#define AP_VERSION "220.68"
#define AP_PI "2e388006-13ba-4041-9a67-25dd4a43d536"
// features bits as UxPlay sets them: mirroring, FairPlay auth, legacy pairing (bit 27) ...
#define AP_FEATURES 0x5A7FFEE6ull

extern airplay_cfg ap_cfg;
extern uint8_t ap_pk[32];  // Ed25519 public key
extern uint8_t ap_sk[64];  // Ed25519 secret key (seed + pk)
void ap_log(const char *fmt, ...);

// ---- pairing.c (legacy pair-setup / pair-verify; Monocypher + tiny-AES) ----
enum { PAIR_INITIAL, PAIR_SETUP, PAIR_HANDSHAKE, PAIR_FINISHED };
typedef struct {
  int status;
  uint8_t our_priv[32], our_pub[32], their_pub[32], their_ed[32], secret[32];
} ap_pairing;

bool ap_crypto_init(const uint8_t seed[32]);
void ap_sha512(const void *a, size_t an, const void *b, size_t bn, uint8_t out[64]);
// Step 1: their X25519 + Ed25519 keys in, our X25519 key + encrypted signature (96 bytes) out.
bool ap_pair_verify1(ap_pairing *p, const uint8_t their_ecdh[32], const uint8_t their_ed[32], uint8_t out[96]);
// Step 2: their encrypted signature in.
bool ap_pair_verify2(ap_pairing *p, const uint8_t sig[64]);

// AES-128-CTR with mbedTLS semantics: the keystream continues across calls.
typedef struct {
  struct AES_ctx ctx;  // tiny-AES key schedule
  uint8_t iv[16], stream[16];
  size_t off;
} ap_aes_ctr;
void ap_aes_ctr_init(ap_aes_ctr *a, const uint8_t key[16], const uint8_t iv[16]);
void ap_aes_ctr_crypt(ap_aes_ctr *a, uint8_t *buf, size_t n);

// ---- fairplay.c ----
int fairplay_setup(const uint8_t req[16], uint8_t res[142]);
int fairplay_handshake(uint8_t keymsg[164], const uint8_t req[164], uint8_t res[32]);
void fairplay_decrypt(const uint8_t keymsg[164], const uint8_t in[72], uint8_t out[16]);

// ---- mirror.c (mirror data channel parser, NTP timing) ----
bool ap_mirror_start(const uint8_t aeskey[16], uint64_t stream_id, uint32_t remote_ip, uint16_t timing_rport);
void ap_mirror_stop(void);
bool ap_mirror_running(void);
bool ap_mirror_streaming(void);

// ---- mdns.c (TXT records; the App advertises them) ----
// TXT record of _airplay._tcp (airplay=true) or _raop._tcp in DNS wire format (len-prefixed "k=v").
size_t ap_txt_record(bool airplay, uint8_t *out, size_t cap);
void ap_deviceid(char out[18]);  // "AA:BB:CC:DD:EE:FF"
void ap_raop_instance(char *out, size_t cap);  // "AABBCCDDEEFF@<name>"

// ---- rtsp.c / handlers.c ----
#define AP_MAX_HDRS 24
typedef struct {
  const char *method, *url, *proto;
  const char *names[AP_MAX_HDRS], *vals[AP_MAX_HDRS];
  int nhdr;
  const uint8_t *body;
  size_t body_len;
} ap_req;

typedef struct {
  int code;
  const char *reason;
  char hdrs[512];
  size_t hlen;
  uint8_t *body;  // malloc'ed, freed by the server
  size_t body_len;
  bool close;
} ap_resp;

typedef struct {
  uint32_t sock;       // App socket handle, 0 = free slot
  uint32_t remote_ip;  // network byte order
  uint8_t *buf;
  size_t len, cap;
  ap_pairing pair;
  uint8_t keymsg[164];
  bool have_keymsg;
  uint8_t aeskey[16];  // FairPlay-decrypted (and pairing-hashed) session key
  bool have_aes;
  uint16_t timing_rport;
} ap_conn;

extern ap_conn *ap_mirror_owner;  // connection whose SETUP started the mirror stream

const char *ap_header(const ap_req *r, const char *name);
void ap_resp_header(ap_resp *r, const char *name, const char *val);
void ap_handle(ap_conn *c, const ap_req *req, ap_resp *res);

// ---- entry points for the App (airplay_host.cpp) ----
ap_conn *ap_conn_open(uint32_t sock, uint32_t remote_ip);  // NULL when every slot is taken
bool ap_conn_data(ap_conn *c, const uint8_t *p, size_t n);  // false: close the connection
void ap_conn_close(ap_conn *c);                             // also stops a mirror it owns
bool ap_mirror_feed(const uint8_t *p, size_t n);            // bytes of the data connection; false: close it
void ap_mirror_data_closed(void);
void ap_mirror_ntp_reply(const uint8_t *p, size_t n);       // a datagram on the timing port
void ap_mirror_tick(void);                                  // call often; sends NTP every 3 s

// ---- host hooks, implemented by the App (airplay_host.cpp) ----
uint64_t ap_now_us(void);
void ap_random(uint8_t *out, size_t n);
bool ap_send(uint32_t sock, const void *p, size_t n);  // whole buffer or false
void ap_socket_close(uint32_t sock);
bool ap_host_mirror_open(void);  // mirror listener + timing and audio datagram sockets
void ap_host_mirror_close(void);
void ap_host_send_ntp(const uint8_t req[32], uint32_t ip, uint16_t port);
// Direct calls instead of airplay_cfg's callbacks: the AOT compiler takes no
// indirect calls in this encoding, so the receiver calls these by name.
void ap_host_on_config(const uint8_t *sps_pps, size_t len);
void ap_host_on_au(const uint8_t *au, size_t len, bool idr);
void ap_host_log(const char *msg);

#ifdef __cplusplus
}
#endif
