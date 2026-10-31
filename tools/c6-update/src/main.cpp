// Updates the ESP32-C6 WiFi co-processor of the 4" board to the ESP-Hosted
// firmware matching the P4's Arduino core, with the core's own
// ESP_HostedOTA library (it downloads the image from Espressif's GitHub
// pages). See README.md.

#include <Arduino.h>
#include <WiFi.h>

#include "ESP_HostedOTA.h"
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
  if (updateEspHostedSlave()) {
    Serial.println("DONE: updated; restarting to check the new version");
    delay(500);
    ESP.restart();
  }
  Serial.println("FAILED: the update did not complete (see above)");
}

void loop() { delay(1000); }
