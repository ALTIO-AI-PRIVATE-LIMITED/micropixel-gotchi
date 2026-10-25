/*
 * AirPlay legacy pairing (pair-setup / pair-verify) and the AES-CTR helper.
 * Ported from UxPlay lib/pairing.c. Copyright (C) 2018 Juho Vähä-Herttua, (C) 2020 Jaslo Ziska,
 * (C) 2021-2025 F. Duncanh; ESP32 port 2026 (ESP32 iPhone Agent); MicroPixel port 2026:
 * Monocypher replaces libsodium and mbedTLS SHA-512, tiny-AES replaces mbedTLS AES.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#include <string.h>
#include "../vendor/monocypher/monocypher.h"
#include "../vendor/monocypher/monocypher-ed25519.h"
#include "ap_internal.h"

#define SALT_KEY "Pair-Verify-AES-Key"
#define SALT_IV "Pair-Verify-AES-IV"

uint8_t ap_pk[32];
uint8_t ap_sk[64];

bool ap_crypto_init(const uint8_t seed[32]) {
  uint8_t copy[32];
  memcpy(copy, seed, 32);  // Monocypher wipes the seed it is given
  crypto_ed25519_key_pair(ap_sk, ap_pk, copy);
  return true;
}

void ap_sha512(const void *a, size_t an, const void *b, size_t bn, uint8_t out[64]) {
  crypto_sha512_ctx ctx;
  crypto_sha512_init(&ctx);
  crypto_sha512_update(&ctx, a, an);
  if (bn) crypto_sha512_update(&ctx, b, bn);
  crypto_sha512_final(&ctx, out);
}

void ap_aes_ctr_init(ap_aes_ctr *a, const uint8_t key[16], const uint8_t iv[16]) {
  AES_init_ctx(&a->ctx, key);
  memcpy(a->iv, iv, 16);
  a->off = 0;
}

// As mbedtls_aes_crypt_ctr(): one keystream block per 16 bytes, the 128-bit counter
// incremented big-endian, and a partial block carried into the next call.
void ap_aes_ctr_crypt(ap_aes_ctr *a, uint8_t *buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (a->off == 0) {
      memcpy(a->stream, a->iv, 16);
      AES_ECB_encrypt(&a->ctx, a->stream);
      for (int j = 15; j >= 0; j--)
        if (++a->iv[j] != 0) break;
    }
    buf[i] ^= a->stream[a->off];
    a->off = (a->off + 1) & 15;
  }
}

// AES-128-CTR over `len` bytes of a keystream derived from the shared secret, after
// skipping `skip` bytes (pair-verify step 2 skips the server's own signature block).
static void verify_ctr(const ap_pairing *p, size_t skip, uint8_t *buf, size_t len) {
  uint8_t h[64], key[16], iv[16], waste[64] = {0};
  ap_aes_ctr aes;
  ap_sha512(SALT_KEY, strlen(SALT_KEY), p->secret, 32, h);
  memcpy(key, h, 16);
  ap_sha512(SALT_IV, strlen(SALT_IV), p->secret, 32, h);
  memcpy(iv, h, 16);
  ap_aes_ctr_init(&aes, key, iv);
  if (skip) ap_aes_ctr_crypt(&aes, waste, skip);
  ap_aes_ctr_crypt(&aes, buf, len);
}

bool ap_pair_verify1(ap_pairing *p, const uint8_t their_ecdh[32], const uint8_t their_ed[32], uint8_t out[96]) {
  static const uint8_t zero[32] = {0};
  uint8_t msg[64];
  if (p->status == PAIR_FINISHED) return false;
  memcpy(p->their_pub, their_ecdh, 32);
  memcpy(p->their_ed, their_ed, 32);
  ap_random(p->our_priv, 32);
  crypto_x25519_public_key(p->our_pub, p->our_priv);
  crypto_x25519(p->secret, p->our_priv, p->their_pub);
  if (!memcmp(p->secret, zero, 32)) return false;  // low-order point, as libsodium rejects it
  p->status = PAIR_HANDSHAKE;

  // sign both ECDH public keys (ours first), then encrypt the signature
  memcpy(msg, p->our_pub, 32);
  memcpy(msg + 32, p->their_pub, 32);
  memcpy(out, p->our_pub, 32);
  crypto_ed25519_sign(out + 32, ap_sk, msg, 64);
  verify_ctr(p, 0, out + 32, 64);
  return true;
}

bool ap_pair_verify2(ap_pairing *p, const uint8_t sig[64]) {
  uint8_t msg[64], s[64];
  if (p->status != PAIR_HANDSHAKE) return false;
  memcpy(s, sig, 64);
  verify_ctr(p, 64, s, 64);  // one "fake round" for the handshake signature, as in UxPlay
  memcpy(msg, p->their_pub, 32);
  memcpy(msg + 32, p->our_pub, 32);
  if (crypto_ed25519_check(s, p->their_ed, msg, 64) != 0) return false;
  p->status = PAIR_FINISHED;
  return true;
}
