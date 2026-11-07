// SPDX-License-Identifier: EUPL-1.2
#include "rns_crypto.h"

#include <string.h>

#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#include "port/random.h"

namespace crewradio {
namespace rns {

bool sha256(uint8_t out[32], const Part* parts, size_t n) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
  for (size_t i = 0; ok && i < n; i++) {
    if (parts[i].len > 0) {
      ok = mbedtls_sha256_update(&ctx, parts[i].p, parts[i].len) == 0;
    }
  }
  ok = ok && mbedtls_sha256_finish(&ctx, out) == 0;
  mbedtls_sha256_free(&ctx);
  if (!ok) mbedtls_platform_zeroize(out, 32);
  return ok;
}

bool sha256(uint8_t out[32], const uint8_t* p, size_t len) {
  const Part part = {p, len};
  return sha256(out, &part, 1);
}

bool truncated_hash(uint8_t out[kHashBytes], const Part* parts, size_t n) {
  uint8_t full[32];
  const bool ok = sha256(full, parts, n);
  memcpy(out, full, kHashBytes);
  return ok;
}

bool truncated_hash(uint8_t out[kHashBytes], const uint8_t* p, size_t len) {
  const Part part = {p, len};
  return truncated_hash(out, &part, 1);
}

// RFC 2104 over SHA-256 itself rather than mbedtls_md: the md layer
// allocates its context, and a failed allocation left `out` as whatever the
// stack held (a tag from an earlier packet, as it happened), which a MAC
// check then compared against. Nothing here allocates.
bool hmac_sha256(uint8_t out[32], const uint8_t* key, size_t key_len,
                 const Part* parts, size_t n) {
  constexpr size_t kBlock = 64;
  constexpr size_t kMaxParts = 4;
  uint8_t k[kBlock] = {0};
  bool ok = n <= kMaxParts;
  if (key_len > kBlock) {
    ok = ok && sha256(k, key, key_len);  // A long key is its hash (RFC 2104)
  } else if (key_len > 0) {
    memcpy(k, key, key_len);
  }
  uint8_t pad[kBlock];
  for (size_t i = 0; i < kBlock; i++) pad[i] = (uint8_t)(k[i] ^ 0x36);
  uint8_t inner[32];
  if (ok) {
    Part all[kMaxParts + 1];
    all[0] = {pad, kBlock};
    for (size_t i = 0; i < n; i++) all[1 + i] = parts[i];
    ok = sha256(inner, all, n + 1);
  }
  for (size_t i = 0; i < kBlock; i++) pad[i] = (uint8_t)(k[i] ^ 0x5C);
  const Part outer[2] = {{pad, kBlock}, {inner, sizeof(inner)}};
  ok = ok && sha256(out, outer, 2);
  mbedtls_platform_zeroize(k, sizeof(k));
  mbedtls_platform_zeroize(pad, sizeof(pad));
  mbedtls_platform_zeroize(inner, sizeof(inner));
  if (!ok) mbedtls_platform_zeroize(out, 32);
  return ok;
}

bool hkdf(uint8_t* out, size_t len, const uint8_t* ikm, size_t ikm_len,
          const uint8_t* salt, size_t salt_len, const uint8_t* info,
          size_t info_len) {
  static const uint8_t kZeros[32] = {0};
  uint8_t prk[32];
  const Part ikm_part = {ikm, ikm_len};
  bool ok = salt_len == 0
                ? hmac_sha256(prk, kZeros, sizeof(kZeros), &ikm_part, 1)
                : hmac_sha256(prk, salt, salt_len, &ikm_part, 1);
  uint8_t block[32];
  size_t block_len = 0;
  size_t done = 0;
  for (uint8_t i = 1; ok && done < len; i++) {
    const Part parts[3] = {{block, block_len}, {info, info_len}, {&i, 1}};
    ok = hmac_sha256(block, prk, sizeof(prk), parts, 3);
    block_len = sizeof(block);
    const size_t n = len - done < block_len ? len - done : block_len;
    memcpy(out + done, block, n);
    done += n;
  }
  mbedtls_platform_zeroize(prk, sizeof(prk));
  mbedtls_platform_zeroize(block, sizeof(block));
  if (!ok) mbedtls_platform_zeroize(out, len);
  return ok;
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
  bool ok = mbedtls_aes_setkey_enc(&aes, key + 32, 256) == 0 &&
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded, chain,
                                  body, body) == 0;
  mbedtls_aes_free(&aes);
  const Part signed_part = {out, kIvBytes + padded};
  ok = ok && hmac_sha256(out + kIvBytes + padded, key, 32, &signed_part, 1);
  if (!ok) {
    // Never a token with the plaintext still in it, or an unsigned one
    mbedtls_platform_zeroize(out, kIvBytes + padded + kMacBytes);
    return 0;
  }
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
  if (!hmac_sha256(mac, key, 32, &signed_part, 1)) return -1;
  if (!equal(mac, token + body, kMacBytes)) return -1;
  const size_t cipher = body - kIvBytes;
  uint8_t chain[kIvBytes];
  memcpy(chain, token, kIvBytes);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  int rc = mbedtls_aes_setkey_dec(&aes, key + 32, 256);
  if (rc == 0) {
    rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipher, chain,
                               token + kIvBytes, out);
  }
  mbedtls_aes_free(&aes);
  if (rc != 0) return -1;
  return unpad_len(out, cipher);
}

}  // namespace rns
}  // namespace crewradio
