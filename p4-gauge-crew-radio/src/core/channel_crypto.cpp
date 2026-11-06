// SPDX-License-Identifier: EUPL-1.2
#include "channel_crypto.h"

#include <string.h>

#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

#include "../port/random.h"

namespace crewradio {

namespace {

constexpr char kSalt[] = "CrewRadio channel key v4";
constexpr char kReticulumLabel[] = "CrewRadio reticulum v1";
constexpr char kReticulumConfirmLabel[] = "CrewRadio reticulum confirm v1";

const mbedtls_md_info_t* sha256() {
  return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
}

}  // namespace

bool ChannelCrypto::derive(const uint8_t* passphrase, size_t len,
                           uint8_t out[kKeyBytes], uint32_t iterations,
                           void (*pause)()) {
  if (iterations < 1) return false;
  // HMAC zero-pads its key, so one zero byte is the same key as none: what
  // PBKDF2 defines for an empty password (and what the app does).
  const uint8_t zero = 0;
  const uint8_t* key = len == 0 ? &zero : passphrase;
  const size_t key_len = len == 0 ? 1 : len;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  bool ok = mbedtls_md_setup(&ctx, sha256(), 1) == 0 &&
            mbedtls_md_hmac_starts(&ctx, key, key_len) == 0;
  // One block: the key is exactly one hash long. U1 = HMAC(P, salt || 1).
  uint8_t u[32];
  const uint8_t block[4] = {0, 0, 0, 1};
  ok = ok &&
       mbedtls_md_hmac_update(&ctx, (const uint8_t*)kSalt,
                              sizeof(kSalt) - 1) == 0 &&
       mbedtls_md_hmac_update(&ctx, block, sizeof(block)) == 0 &&
       mbedtls_md_hmac_finish(&ctx, u) == 0;
  if (ok) memcpy(out, u, sizeof(u));
  for (uint32_t i = 2; ok && i <= iterations; i++) {
    ok = mbedtls_md_hmac_reset(&ctx) == 0 &&
         mbedtls_md_hmac_update(&ctx, u, sizeof(u)) == 0 &&
         mbedtls_md_hmac_finish(&ctx, u) == 0;
    for (size_t j = 0; j < sizeof(u); j++) out[j] ^= u[j];
    if (pause != nullptr && i % kPauseEvery == 0) pause();
  }
  mbedtls_md_free(&ctx);
  mbedtls_platform_zeroize(u, sizeof(u));
  if (!ok) mbedtls_platform_zeroize(out, kKeyBytes);
  return ok;
}

ChannelCrypto::ChannelCrypto(const uint8_t key[kKeyBytes]) {
  memcpy(key_, key, kKeyBytes);
  mbedtls_gcm_init(&gcm_);
  ok_ = mbedtls_gcm_setkey(&gcm_, MBEDTLS_CIPHER_ID_AES, key_,
                           kKeyBytes * 8) == 0;
}

ChannelCrypto::~ChannelCrypto() {
  mbedtls_gcm_free(&gcm_);
  mbedtls_platform_zeroize(key_, sizeof(key_));
}

bool ChannelCrypto::seal(const uint8_t* aad, size_t aad_len,
                         const uint8_t* plain, size_t len, uint8_t* out) {
  uint8_t nonce[kNonceBytes];
  port::random_bytes(nonce, sizeof(nonce));
  return seal_with_nonce(aad, aad_len, plain, len, nonce, out);
}

bool ChannelCrypto::seal_with_nonce(const uint8_t* aad, size_t aad_len,
                                    const uint8_t* plain, size_t len,
                                    const uint8_t nonce[kNonceBytes],
                                    uint8_t* out) {
  if (!ok_) return false;
  memcpy(out, nonce, kNonceBytes);
  return mbedtls_gcm_crypt_and_tag(&gcm_, MBEDTLS_GCM_ENCRYPT, len, nonce,
                                   kNonceBytes, aad, aad_len, plain,
                                   out + kNonceBytes, kTagBytes,
                                   out + kNonceBytes + len) == 0;
}

int ChannelCrypto::open(const uint8_t* aad, size_t aad_len,
                        const uint8_t* sealed, size_t len, uint8_t* out) {
  if (!ok_ || sealed == nullptr || len < kOverhead + 1) return -1;
  const size_t plain_len = len - kOverhead;
  const int r = mbedtls_gcm_auth_decrypt(
      &gcm_, plain_len, sealed, kNonceBytes, aad, aad_len,
      sealed + kNonceBytes + plain_len, kTagBytes, sealed + kNonceBytes, out);
  if (r != 0) {
    // mbedTLS wipes the output on a bad tag; say so anyway: nothing of a
    // packet that failed may be used.
    return -1;
  }
  return (int)plain_len;
}

bool ChannelCrypto::hmac(const uint8_t* data, size_t len,
                         uint8_t out[32]) const {
  return mbedtls_md_hmac(sha256(), key_, kKeyBytes, data, len, out) == 0;
}

bool ChannelCrypto::reticulum_tag(char out[17]) const {
  uint8_t mac[32];
  if (!hmac((const uint8_t*)kReticulumLabel, sizeof(kReticulumLabel) - 1,
            mac)) {
    return false;
  }
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    out[2 * i] = kHex[mac[i] >> 4];
    out[2 * i + 1] = kHex[mac[i] & 15];
  }
  out[16] = '\0';
  return true;
}

bool ChannelCrypto::reticulum_confirm_key(uint8_t out[32]) const {
  return hmac((const uint8_t*)kReticulumConfirmLabel,
              sizeof(kReticulumConfirmLabel) - 1, out);
}

}  // namespace crewradio
