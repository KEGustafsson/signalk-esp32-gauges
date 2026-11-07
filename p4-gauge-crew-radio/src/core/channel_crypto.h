// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Confidentiality and authenticity of every packet, from one shared channel
// key: the app's ChannelCrypto.kt over mbedTLS (hardware AES and SHA on the
// ESP32-P4; the same mbedTLS 3.6 the host tests build).
//
// The packet key is PBKDF2-HMAC-SHA256 over the channel key's UTF-8 bytes,
// 600 000 rounds, salt "CrewRadio channel key v4", written out here as in the
// app rather than taken from a library. It is slow on purpose (an offline
// guess costs the same), so the node derives it once, when the key is saved,
// and keeps the result.
//
// One instance per task: the AES-GCM context is not shared between threads.
// The receive task opens, the capture task seals; each builds its own from
// the same 32-byte key.

#include <stddef.h>
#include <stdint.h>

#include <mbedtls/gcm.h>

namespace crewradio {

class ChannelCrypto {
 public:
  static constexpr size_t kKeyBytes = 32;
  static constexpr size_t kNonceBytes = 12;
  static constexpr size_t kTagBytes = 16;
  // Bytes a sealed payload is longer than its plaintext.
  static constexpr size_t kOverhead = kNonceBytes + kTagBytes;
  static constexpr uint32_t kIterations = 600000;

  // The packet key for a channel key (its UTF-8 bytes). False only if
  // mbedTLS fails. `iterations` is for tests of the loop itself; the wire
  // needs kIterations. `pause`, if given, is called every kPauseEvery
  // rounds: on the node it lets the core's idle task run (the derivation
  // takes seconds, and the task watchdog watches the idle tasks).
  static constexpr uint32_t kPauseEvery = 2000;
  static bool derive(const uint8_t* passphrase, size_t len,
                     uint8_t out[kKeyBytes],
                     uint32_t iterations = kIterations,
                     void (*pause)() = nullptr);

  explicit ChannelCrypto(const uint8_t key[kKeyBytes]);
  ~ChannelCrypto();
  ChannelCrypto(const ChannelCrypto&) = delete;
  ChannelCrypto& operator=(const ChannelCrypto&) = delete;

  bool ok() const { return ok_; }

  // nonce || ciphertext || tag into `out` (len + kOverhead bytes), under a
  // fresh random nonce.
  bool seal(const uint8_t* aad, size_t aad_len, const uint8_t* plain,
            size_t len, uint8_t* out);
  // The same with a given nonce: for test vectors only. A nonce must never
  // repeat under one key.
  bool seal_with_nonce(const uint8_t* aad, size_t aad_len,
                       const uint8_t* plain, size_t len,
                       const uint8_t nonce[kNonceBytes], uint8_t* out);

  // The plaintext length (written to `out`, len - kOverhead bytes), or -1 if
  // `sealed` was not sealed with this key and exactly this aad.
  int open(const uint8_t* aad, size_t aad_len, const uint8_t* sealed,
           size_t len, uint8_t* out);

  // HMAC-SHA256 under the packet key.
  bool hmac(const uint8_t* data, size_t len, uint8_t out[32]) const;

  // The channel's Reticulum destination aspect: the first 8 bytes of
  // HMAC(packet key, "CrewRadio reticulum v1"), lower-case hex.
  bool reticulum_tag(char out[17]) const;
  // HMAC(packet key, "CrewRadio reticulum confirm v1"): the key of each
  // Reticulum link's key proof.
  bool reticulum_confirm_key(uint8_t out[32]) const;

 private:
  uint8_t key_[kKeyBytes];
  mbedtls_gcm_context gcm_;
  bool ok_ = false;
};

}  // namespace crewradio
