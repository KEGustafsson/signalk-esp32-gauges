// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "random.h"

#include <bootloader_random.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <mbedtls/hmac_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

namespace crewradio {
namespace port {

namespace {

constexpr const char* kTag = "crewradio";
constexpr size_t kSeedBytes = 64;
constexpr size_t kAddBytes = 32;

// The ESP32-P4 has no radio of its own (its WiFi is the C6's), and ESP-IDF
// guarantees esp_fill_random() true random only while the RF subsystem or
// the SAR ADC entropy source is on; the bootloader turns the ADC source off
// before the app runs, leaving a pseudo-random generator. So: the ADC source
// on just long enough to seed an HMAC-DRBG (ESP-IDF's own advice), off again
// (nothing on the 4" uses the ADC), and every draw from the DRBG with fresh
// hardware bytes as additional input, so it is never weaker than the
// hardware RNG alone.
struct Drbg {
  mbedtls_hmac_drbg_context ctx;
  SemaphoreHandle_t lock = nullptr;
  bool ok = false;

  Drbg() {
    mbedtls_hmac_drbg_init(&ctx);
    lock = xSemaphoreCreateMutex();
    uint8_t seed[kSeedBytes];
    bootloader_random_enable();
    esp_fill_random(seed, sizeof(seed));
    bootloader_random_disable();
    ok = lock != nullptr &&
         mbedtls_hmac_drbg_seed_buf(
             &ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), seed,
             sizeof(seed)) == 0;
    mbedtls_platform_zeroize(seed, sizeof(seed));
    if (!ok) {
      ESP_LOGE(kTag, "random: DRBG not seeded; using the hardware RNG alone");
    }
  }
};

Drbg& drbg() {
  static Drbg d;  // Seeded on first use, thread-safe (C++ static init)
  return d;
}

}  // namespace

void random_bytes(uint8_t* out, size_t len) {
  Drbg& d = drbg();
  bool ok = d.ok && xSemaphoreTake(d.lock, portMAX_DELAY) == pdTRUE;
  if (ok) {
    uint8_t add[kAddBytes];
    size_t done = 0;
    while (ok && done < len) {
      size_t n = len - done;
      if (n > MBEDTLS_HMAC_DRBG_MAX_REQUEST) n = MBEDTLS_HMAC_DRBG_MAX_REQUEST;
      esp_fill_random(add, sizeof(add));
      ok = mbedtls_hmac_drbg_random_with_add(&d.ctx, out + done, n, add,
                                             sizeof(add)) == 0;
      done += n;
    }
    mbedtls_platform_zeroize(add, sizeof(add));
    xSemaphoreGive(d.lock);
  }
  // Never fewer random bytes than asked for: the hardware RNG on its own
  if (!ok) esp_fill_random(out, len);
}

}  // namespace port
}  // namespace crewradio
#endif
