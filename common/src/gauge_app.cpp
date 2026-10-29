#include "gauge_app.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <lvgl.h>

#include <memory>
#include <vector>

#include "gauge_secrets.h"
#include "gauge_ui.h"
#include "sensesp_app_builder.h"
#include "sk_data.h"
#include "status_led.h"

namespace skgauges {

namespace {

std::vector<sensesp::ClientSSIDConfig> wifi_clients(const char* address) {
  const bool use_static = SECRET_USE_STATIC_IP && address != nullptr &&
                          address[0] != '\0';
  IPAddress ip, netmask, gateway, dns;
  if (use_static) {
    if (!ip.fromString(address) || !netmask.fromString(SECRET_NETMASK) ||
        !gateway.fromString(SECRET_GATEWAY) || !dns.fromString(SECRET_DNS)) {
      ESP_LOGE("gauge", "Bad static IP settings in secrets.h, using DHCP");
      return wifi_clients(nullptr);
    }
  }
  const char* const networks[3][2] = {
      {SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD},
      {SECRET_WIFI_SSID_2, SECRET_WIFI_PASSWORD_2},
      {SECRET_WIFI_SSID_3, SECRET_WIFI_PASSWORD_3},
  };
  std::vector<sensesp::ClientSSIDConfig> clients;
  for (const auto& n : networks) {
    if (use_static) {
      clients.emplace_back(n[0], n[1], false, ip, netmask, gateway, dns);
    } else {
      clients.emplace_back(n[0], n[1]);
    }
  }
  return clients;
}

// A loop that stops turning would leave the last numbers on screen forever,
// so the Arduino loop task runs under the task watchdog (5 s): a hang reboots
// the unit. An OTA upload legitimately blocks the loop for its whole
// transfer, so the watchdog is off while one runs. Registered after
// SensESP's own OTA setup (its onDelay(0)), so these callbacks are the ones
// ArduinoOTA keeps.
void start_loop_watchdog() {
  enableLoopWDT();
  if (SECRET_OTA_PASSWORD[0] == '\0') {
    return;
  }
  sensesp::event_loop()->onDelay(1, []() {
    ArduinoOTA.onStart([]() {
      disableLoopWDT();
      ESP_LOGW("gauge", "OTA upload started");
    });
    ArduinoOTA.onError([](ota_error_t error) {
      ESP_LOGE("gauge", "OTA upload failed (%u)", error);
      enableLoopWDT();
    });
  });
}

}  // namespace

void gauge_app_begin(const GaugeAppConfig& config) {
  // UI first, so the gauge is on screen while WiFi and Signal K connect.
  ui_begin({config.side, config.unit_name, config.set_brightness});
  lv_timer_handler();

  sensesp::SensESPAppBuilder builder;
  builder.set_hostname(config.hostname)
      ->set_wifi_clients(wifi_clients(config.address))
      // SensESP's status LED, shown as the dot at the bottom of the bezel.
      ->set_system_status_led(std::make_shared<ScreenStatusLed>())
      ->set_button_pin(config.button_pin);
  if (SECRET_SK_SERVER_ADDRESS[0] != '\0') {
    builder.set_sk_server(SECRET_SK_SERVER_ADDRESS, SECRET_SK_SERVER_PORT);
  }
  if (SECRET_OTA_PASSWORD[0] != '\0') {
    builder.enable_ota(SECRET_OTA_PASSWORD);
  }
  // SensESP's setup access point. With a password from secrets.h it uses
  // that; with the boat's WiFi configured and no AP password it is off (as in
  // drive-remote-controller); with nothing configured it keeps SensESP's
  // default so a fresh unit can still be set up.
  if (SECRET_AP_PASSWORD[0] != '\0') {
    builder.set_wifi_access_point(config.hostname, SECRET_AP_PASSWORD);
  } else if (SECRET_WIFI_SSID[0] != '\0') {
    builder.set_wifi_access_point("", "");
  }
  // Without this the web UI (which can repoint the gauge at another Signal K
  // server) is open to anyone on the network.
  if (SECRET_ADMIN_PASSWORD[0] != '\0') {
    builder.set_admin_user(SECRET_ADMIN_USER, SECRET_ADMIN_PASSWORD);
  }
  sensesp::sensesp_app = builder.get_app();

  sk_data().begin(config.side);
  start_loop_watchdog();
}

void gauge_app_loop() {
  sensesp::event_loop()->tick();
  const uint32_t wait_ms = lv_timer_handler();
  // Sleep until the next LVGL timer is due, but at most 5 ms so SensESP's
  // event loop keeps ticking often.
  delay(wait_ms == 0 ? 1 : (wait_ms < 5 ? wait_ms : 5));
}

}  // namespace skgauges
