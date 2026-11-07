// SPDX-License-Identifier: EUPL-1.2
#include "rns_crypto.h"

#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include "port/random.h"

namespace crewradio {
namespace rns {

void sha256(uint8_t out[32], const Part* parts, size_t n) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  for (size_t i = 0; i < n; i++) {
    if (parts[i].len > 0) mbedtls_sha256_update(&ctx, parts[i].p, parts[i].len);
  }
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

void sha256(uint8_t out[32], const uint8_t* p, size_t len) {
  const Part part = {p, len};
  sha256(out, &part, 1);
}

void truncated_hash(uint8_t out[kHashBytes], const Part* parts, size_t n) {
  uint8_t full[32];
  sha256(full, parts, n);
  memcpy(out, full, kHashBytes);
}

void truncated_hash(uint8_t out[kHashBytes], const uint8_t* p, size_t len) {
  const Part part = {p, len};
  truncated_hash(out, &part, 1);
}

void hmac_sha256(uint8_t out[32], const uint8_t* key, size_t key_len,
                 const Part* parts, size_t n) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, key, key_len);
  for (size_t i = 0; i < n; i++) {
    if (parts[i].len > 0) mbedtls_md_hmac_update(&ctx, parts[i].p, parts[i].len);
  }
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

void hkdf(uint8_t* out, size_t len, const uint8_t* ikm, size_t ikm_len,
          const uint8_t* salt, size_t salt_len, const uint8_t* info,
          size_t info_len) {
  static const uint8_t kZeros[32] = {0};
  uint8_t prk[32];
  const Part ikm_part = {ikm, ikm_len};
  if (salt_len == 0) {
    hmac_sha256(prk, kZeros, sizeof(kZeros), &ikm_part, 1);
  } else {
    hmac_sha256(prk, salt, salt_len, &ikm_part, 1);
  }
  uint8_t block[32];
  size_t block_len = 0;
  size_t done = 0;
  for (uint8_t i = 1; done < len; i++) {
    const Part parts[3] = {{block, block_len}, {info, info_len}, {&i, 1}};
    hmac_sha256(block, prk, sizeof(prk), parts, 3);
    block_len = sizeof(block);
    const size_t n = len - done < block_len ? len - done : block_len;
    memcpy(out + done, block, n);
    done += n;
  }
  memset(prk, 0, sizeof(prk));
  memset(block, 0, sizeof(block));
}

bool equal(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t d = 0;
  for (size_t i = 0; i < len; i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

size_t token_encrypt(const uint8_t key[64], const uint8_t* plain, size_t len,
                     uint8_t* out, const uint8_t* iv) {
  const size_t padded = (len / 16 + 1) * 16;
  if (iv != nullptr) {
    memcpy(out, iv, kIvBytes);
  } else {
    port::random_bytes(out, kIvBytes);
  }
  uint8_t* body = out + kIvBytes;
  if (len > 0) memcpy(body, plain, len);
  const uint8_t pad = (uint8_t)(padded - len);
  memset(body + len, pad, pad);
  uint8_t chain[kIvBytes];
  memcpy(chain, out, kIvBytes);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key + 32, 256);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded, chain, body, body);
  mbedtls_aes_free(&aes);
  const Part signed_part = {out, kIvBytes + padded};
  hmac_sha256(out + kIvBytes + padded, key, 32, &signed_part, 1);
  return kIvBytes + padded + kMacBytes;
}

int unpad_len(const uint8_t* padded, size_t len) {
  if (len == 0 || len % 16 != 0) return -1;
  const uint8_t n = padded[len - 1];
  if (n < 1 || n > 16) return -1;
  for (size_t i = len - n; i < len; i++) {
    if (padded[i] != n) return -1;
  }
  return (int)(len - n);
}

int token_decrypt(const uint8_t key[64], const uint8_t* token, size_t len,
                  uint8_t* out) {
  if (len < kTokenOverhead + 16 || (len - kTokenOverhead) % 16 != 0) return -1;
  const size_t body = len - kMacBytes;
  uint8_t mac[32];
  const Part signed_part = {token, body};
  hmac_sha256(mac, key, 32, &signed_part, 1);
  if (!equal(mac, token + body, kMacBytes)) return -1;
  const size_t cipher = body - kIvBytes;
  uint8_t chain[kIvBytes];
  memcpy(chain, token, kIvBytes);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key + 32, 256);
  const int rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher,
                                       chain, token + kIvBytes, out);
  mbedtls_aes_free(&aes);
  if (rc != 0) return -1;
  return unpad_len(out, cipher);
}

}  // namespace rns
}  // namespace crewradio
