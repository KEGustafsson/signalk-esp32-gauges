#include "gauge_app.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <lvgl.h>

#include <memory>
#include <vector>

#include "gauge_secrets.h"
#include "gauge_settings.h"
#include "gauge_ui.h"
#include "sensesp/system/resettable.h"
#include "sensesp/ui/config_item.h"
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
bool loop_watchdog_on = false;
bool ota_started = false;  // An upload got past authentication

void loop_watchdog(bool on) {
  if (on == loop_watchdog_on) return;  // ArduinoOTA may report one error twice
  loop_watchdog_on = on;
  if (on) {
    enableLoopWDT();
  } else {
    disableLoopWDT();
  }
}

// A factory reset (web UI, or the S3's BOOT key held 5 s) formats the whole
// filesystem on the loop task, which takes longer than the watchdog allows.
// Highest priority, so it runs before SensESP's own resets.
class WatchdogOffForReset : public sensesp::Resettable {
 public:
  WatchdogOffForReset() : sensesp::Resettable(1000) {}
  void reset() override { loop_watchdog(false); }
};

void start_loop_watchdog() {
  loop_watchdog(true);
  new WatchdogOffForReset();  // Registers itself; lives for the program
  if (SECRET_OTA_PASSWORD[0] == '\0') {
    return;
  }
  sensesp::event_loop()->onDelay(1, []() {
    ArduinoOTA.onStart([]() {
      ota_started = true;
      loop_watchdog(false);
      ESP_LOGW("gauge", "OTA upload started");
      // The upload blocks the loop: replace the gauge with a notice rather
      // than leave its last numbers on screen looking live.
      ui_show_notice("Updating firmware...");
    });
    ArduinoOTA.onError([](ota_error_t error) {
      // A bad password or a refused begin fails before onStart: the gauge
      // never stopped, so it carries on (and nobody on the network can
      // reboot it by knocking with a wrong password).
      if (!ota_started) {
        ESP_LOGW("gauge", "OTA invitation refused (%u)", error);
        return;
      }
      ESP_LOGE("gauge", "OTA upload failed (%u), restarting", error);
      // The gauge page is gone; a restart is the simplest way back.
      delay(1000);
      esp_restart();
    });
  });
}

// The HTTP server's settings as the authenticator was built from them at
// boot: the saved file, or SensESP's defaults (no login) when there is none
// -- load() leaves memory untouched without a file, so reset it first.
String saved_web_ui_login(sensesp::ConfigItemBase* item) {
  JsonDocument defaults_doc;
  JsonObject defaults = defaults_doc.to<JsonObject>();
  defaults["auth_required"] = false;
  defaults["username"] = "";
  defaults["ha1"] = "";
  item->from_json(defaults);
  item->load();
  JsonDocument saved_doc;
  JsonObject saved = saved_doc.to<JsonObject>();
  item->to_json(saved);
  String json;
  serializeJson(saved, json);
  return json;
}

// SensESP builds the web UI's authenticator only from its saved settings,
// when the HTTP server is constructed: set_admin_user() alone never takes
// effect. So the wanted login (or "no login") is compared with what is saved,
// and if it differs it is saved and the unit restarts once to apply it.
void apply_web_ui_login() {
  auto item = sensesp::ConfigItemBase::get_config_item("/system/httpserver");
  if (!item) {
    ESP_LOGE("gauge", "No HTTP server settings; web UI login not applied");
    return;
  }
  JsonDocument wanted_doc;
  JsonObject wanted = wanted_doc.to<JsonObject>();
  if (SECRET_ADMIN_PASSWORD[0] != '\0') {
    item->to_json(wanted);  // set_admin_user() put the login in memory
  } else {
    wanted["auth_required"] = false;
    wanted["username"] = "";
    wanted["ha1"] = "";
  }
  String wanted_json;
  serializeJson(wanted, wanted_json);
  if (saved_web_ui_login(item.get()) == wanted_json) {
    return;
  }
  item->from_json(wanted);
  item->save();
  // save() reports success even when the file write fails: read it back, so
  // a full or broken filesystem cannot turn this into a boot loop.
  if (saved_web_ui_login(item.get()) != wanted_json) {
    ESP_LOGE("gauge", "Could not save the web UI login; not applied");
    return;
  }
  ESP_LOGW("gauge", "Web UI login changed; restarting to apply it");
  delay(500);
  esp_restart();
}

// True if a WiFi network the unit can actually join is configured (SensESP
// skips entries without a password).
bool wifi_configured() {
  return (SECRET_WIFI_SSID[0] != '\0' && SECRET_WIFI_PASSWORD[0] != '\0') ||
         (SECRET_WIFI_SSID_2[0] != '\0' && SECRET_WIFI_PASSWORD_2[0] != '\0') ||
         (SECRET_WIFI_SSID_3[0] != '\0' && SECRET_WIFI_PASSWORD_3[0] != '\0');
}

}  // namespace

void gauge_app_begin(const GaugeAppConfig& config) {
  // UI first, so the gauge is on screen while WiFi and Signal K connect.
  // Touch only answers once setup() returns and the loop runs, so the boot
  // milestones are logged to show where that time goes.
  // The display itself was started in main.cpp before this.
  ESP_LOGI("boot", "Display ready, building the gauge at %u ms",
           (unsigned)millis());
  // The page settings (web UI) decide what the first page looks like.
  gauge_config().begin();
  ui_begin({config.side, config.unit_name, config.set_brightness});
  lv_timer_handler();
  ESP_LOGI("boot", "Gauge on screen at %u ms", (unsigned)millis());

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
  // that; with a joinable boat WiFi configured and no AP password it is off
  // (as in drive-remote-controller; recovery is then a USB flash); with
  // nothing configured it keeps SensESP's default so a fresh unit can still
  // be set up.
  if (SECRET_AP_PASSWORD[0] != '\0') {
    builder.set_wifi_access_point(config.hostname, SECRET_AP_PASSWORD);
  } else if (wifi_configured()) {
    builder.set_wifi_access_point("", "");
  }
  // Without a login the web UI (which can repoint the gauge at another
  // Signal K server) is open to anyone on the boat's network. Requests that
  // arrive over the setup access point are not asked to log in (SensESP's
  // captive portal); there SECRET_AP_PASSWORD is the protection.
  if (SECRET_ADMIN_PASSWORD[0] != '\0') {
    builder.set_admin_user(SECRET_ADMIN_USER, SECRET_ADMIN_PASSWORD);
  }
  sensesp::sensesp_app = builder.get_app();
  ESP_LOGI("boot", "SensESP set up at %u ms", (unsigned)millis());
  apply_web_ui_login();

  gauge_config().add_config_items();
  sk_data().begin(config.side);
  start_loop_watchdog();
  ESP_LOGI("boot", "Setup done at %u ms; touch answers from here",
           (unsigned)millis());
}

void gauge_app_loop() {
  // Anything that holds the loop this long makes touch feel dead: log it,
  // with the side that held it.
  constexpr uint32_t kStallMs = 150;
  uint32_t start = millis();
  sensesp::event_loop()->tick();
  uint32_t took = millis() - start;
  if (took >= kStallMs) {
    ESP_LOGW("loop", "SensESP held the loop for %u ms", (unsigned)took);
  }
  start = millis();
  const uint32_t wait_ms = lv_timer_handler();
  took = millis() - start;
  if (took >= kStallMs) {
    ESP_LOGW("loop", "LVGL held the loop for %u ms", (unsigned)took);
  }
  // Sleep until the next LVGL timer is due, but at most 5 ms so SensESP's
  // event loop keeps ticking often.
  delay(wait_ms == 0 ? 1 : (wait_ms < 5 ? wait_ms : 5));
}

}  // namespace skgauges
