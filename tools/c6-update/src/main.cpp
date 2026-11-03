// Updates the ESP32-C6 WiFi co-processor of the 4" board to the ESP-Hosted
// firmware matching the P4's Arduino core. The image comes from Espressif's
// GitHub pages (the URL the core's ESP_HostedOTA uses) and is checked
// against a pinned size and SHA-256 before a byte of it reaches the C6:
// ESP_HostedOTA's own updateEspHostedSlave() skips TLS certificate checks
// and writes whatever it is served. See README.md.

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <string.h>

#include "esp32-hal-hosted.h"
#include "gauge_secrets.h"

namespace {

struct Network {
  const char* ssid;
  const char* password;
};

constexpr Network kNetworks[] = {
    {SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD},
    {SECRET_WIFI_SSID_2, SECRET_WIFI_PASSWORD_2},
    {SECRET_WIFI_SSID_3, SECRET_WIFI_PASSWORD_3},
};

// The images this tool will install, by the ESP-Hosted version the P4's core
// expects. Each was downloaded from hostedGetUpdateURL() and from
// raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/hosted/, which
// gave the same bytes. A platform update with another ESP-Hosted version
// needs its row added here (README.md).
struct Image {
  uint32_t major, minor, patch;
  size_t size;
  const char* sha256;  // Lower-case hex
};

constexpr Image kImages[] = {
    {2, 12, 11, 1226800,
     "5b05e1530e122881ffeeea3ce2e930963cef6fcc79f930cf0994bf9cb9d709cb"},
    // Arduino-ESP32 3.3.12 (pioarduino 55.03.312-1), 2026-11-03.
    {2, 12, 13, 1226976,
     "f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9"},
};

void print_versions() {
  uint32_t major, minor, patch;
  hostedGetHostVersion(&major, &minor, &patch);
  Serial.printf("Host (P4) ESP-Hosted: %u.%u.%u\n", (unsigned)major,
                (unsigned)minor, (unsigned)patch);
  hostedGetSlaveVersion(&major, &minor, &patch);
  Serial.printf("Co-processor (C6) ESP-Hosted: %u.%u.%u\n", (unsigned)major,
                (unsigned)minor, (unsigned)patch);
}

bool connect() {
  for (const Network& net : kNetworks) {
    if (net.ssid[0] == '\0') continue;
    Serial.printf("Connecting to %s", net.ssid);
    WiFi.STA.connect(net.ssid, net.password);
    for (int i = 0; i < 40 && WiFi.STA.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.STA.status() == WL_CONNECTED) {
      Serial.print("Connected, IP ");
      Serial.println(WiFi.STA.localIP());
      return true;
    }
    WiFi.STA.disconnect();
  }
  return false;
}

// The pinned image for the version the host expects, or nullptr.
const Image* expected_image() {
  uint32_t major, minor, patch;
  hostedGetHostVersion(&major, &minor, &patch);
  for (const Image& image : kImages) {
    if (image.major == major && image.minor == minor && image.patch == patch) {
      return &image;
    }
  }
  return nullptr;
}

bool sha256_matches(const uint8_t* data, size_t size, const char* hex) {
  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, size);
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  char got[65];
  for (int i = 0; i < 32; i++) snprintf(got + 2 * i, 3, "%02x", digest[i]);
  Serial.printf("SHA-256 %s\n", got);
  return strcmp(got, hex) == 0;
}

// Downloads the whole image into PSRAM; nullptr on any failure.
uint8_t* download(const char* url, size_t size) {
  uint8_t* image = static_cast<uint8_t*>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (image == nullptr) {
    Serial.println("ERROR: no PSRAM for the image");
    return nullptr;
  }
  // The image is checked against its pinned SHA-256 before it is used, so
  // the connection need not be trusted.
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("ERROR: HTTP begin failed");
    free(image);
    return nullptr;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK || http.getSize() != (int)size) {
    Serial.printf("ERROR: HTTP %d, %d bytes (expected %u)\n", code,
                  http.getSize(), (unsigned)size);
    http.end();
    free(image);
    return nullptr;
  }
  NetworkClient* stream = http.getStreamPtr();
  size_t got = 0;
  uint32_t last_data = millis();
  while (got < size && millis() - last_data < 10000) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(1);
      continue;
    }
    const size_t want = available < size - got ? available : size - got;
    const int read = stream->readBytes(image + got, want);
    if (read > 0) {
      got += (size_t)read;
      last_data = millis();
      if (got % (64 * 1024) < (size_t)read) Serial.print(".");
    }
  }
  Serial.println();
  http.end();
  if (got != size) {
    Serial.printf("ERROR: download stopped at %u of %u bytes\n", (unsigned)got,
                  (unsigned)size);
    free(image);
    return nullptr;
  }
  return image;
}

bool write_to_c6(uint8_t* image, size_t size) {
  if (!hostedBeginUpdate()) {
    Serial.println("ERROR: the C6 did not start the update");
    return false;
  }
  constexpr size_t kChunk = 2048;
  for (size_t done = 0; done < size; done += kChunk) {
    const size_t len = size - done < kChunk ? size - done : kChunk;
    if (!hostedWriteUpdate(image + done, (uint32_t)len)) {
      Serial.printf("\nERROR: the C6 refused bytes at %u\n", (unsigned)done);
      return false;
    }
    if (done % (64 * 1024) == 0) Serial.print(".");
  }
  Serial.println();
  if (!hostedEndUpdate()) {
    Serial.println("ERROR: the C6 did not accept the image");
    return false;
  }
  if (!hostedActivateUpdate()) {
    Serial.println("ERROR: the C6 did not activate the image");
    return false;
  }
  return true;
}

bool update_c6() {
  const Image* expected = expected_image();
  if (expected == nullptr) {
    Serial.println("ERROR: no pinned image for this ESP-Hosted version; add "
                   "its size and SHA-256 to kImages (README.md)");
    return false;
  }
  const char* url = hostedGetUpdateURL();
  Serial.printf("Downloading %s\n", url);
  uint8_t* image = download(url, expected->size);
  if (image == nullptr) return false;
  if (!sha256_matches(image, expected->size, expected->sha256)) {
    Serial.println("ERROR: the image does not match its pinned SHA-256; "
                   "nothing was written to the C6");
    free(image);
    return false;
  }
  Serial.println("Image verified; writing it to the C6");
  const bool ok = write_to_c6(image, expected->size);
  free(image);
  return ok;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n*** ESP32-C6 co-processor update ***");
  WiFi.STA.begin();
  print_versions();
  if (!connect()) {
    Serial.println("FAILED: no WiFi network from secrets.h could be joined");
    return;
  }
  if (!hostedHasUpdate()) {
    Serial.println("DONE: the co-processor is up to date, nothing to do");
    return;
  }
  if (update_c6()) {
    Serial.println("DONE: updated; restarting to check the new version");
    delay(500);
    ESP.restart();
  }
  Serial.println("FAILED: the update did not complete (see above)");
}

void loop() { delay(1000); }
