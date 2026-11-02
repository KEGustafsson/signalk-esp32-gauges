#include "link_watch.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_private/system_internal.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#if CONFIG_ESP_HOSTED_ENABLED
// C headers without extern "C" guards of their own.
extern "C" {
#include "esp_hosted_event.h"
#include "esp_hosted_misc.h"
}
#endif

#include "link_policy.h"
#include "sensesp_app.h"
#include "sk_data.h"

namespace skgauges {

namespace {

constexpr const char* kTag = "link";

// Not esp_restart(): its shutdown handlers include ESP-Hosted's WiFi stop, a
// request to a C6 that may be the very thing not answering (see the
// slow-pass restart in gauge_app.cpp).
[[noreturn]] void restart(const char* why) {
  ESP_LOGE(kTag, "%s: restarting", why);
  vTaskDelay(pdMS_TO_TICKS(100));  // Let the log line out
  esp_restart_noos();
}

SkStallWatch sk_watch;

#if CONFIG_ESP_HOSTED_ENABLED

HostedWatch hosted_watch;
esp_timer_handle_t hosted_timer = nullptr;
// Set once a restart is decided; later events change nothing.
const char* volatile hosted_restart_why = nullptr;

void on_hosted_timer(void*) {
  restart(hosted_restart_why != nullptr
              ? hosted_restart_why
              : "No heartbeat from the WiFi chip (C6) for 60 s: its link is "
                "gone");
}

// On the system event task (a small stack): decide, then leave the restart
// to the timer task.
void on_hosted_event(void*, esp_event_base_t, int32_t id, void*) {
  HostedEvent event;
  switch (id) {
    case ESP_HOSTED_EVENT_CP_HEARTBEAT:
      event = HostedEvent::kHeartbeat;
      break;
    case ESP_HOSTED_EVENT_CP_INIT:
      event = HostedEvent::kCoprocessorStart;
      break;
    case ESP_HOSTED_EVENT_TRANSPORT_FAILURE:
      event = HostedEvent::kTransportFailure;
      break;
    default:
      return;
  }
  if (hosted_restart_why != nullptr) {
    return;
  }
  const bool first_beat =
      event == HostedEvent::kHeartbeat && !hosted_watch.beat_seen;
  const bool restart_now = hosted_event(&hosted_watch, event);
  esp_timer_stop(hosted_timer);  // Not running yet before the first beat
  if (restart_now) {
    hosted_restart_why =
        event == HostedEvent::kTransportFailure
            ? "ESP-Hosted reports the link to the WiFi chip (C6) failed"
            : "The WiFi chip (C6) restarted under a running link";
    esp_timer_start_once(hosted_timer, 1000);
    return;
  }
  if (event == HostedEvent::kHeartbeat) {
    esp_timer_start_once(hosted_timer, (uint64_t)kHostedSilenceMs * 1000);
    if (first_beat) {
      ESP_LOGI(kTag, "First heartbeat from the WiFi chip (C6)");
    }
  }
}

// Asking the C6 for heartbeats is a request to it that can wait up to 5 s:
// done once on a task of its own, never on the loop.
void hosted_setup_task(void*) {
  // The link to the C6 is up once WiFi has started (a flag, not a request).
  while (!WiFi.STA.started()) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  const esp_timer_create_args_t args = {
      .callback = on_hosted_timer,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "c6_beat",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&args, &hosted_timer) != ESP_OK ||
      esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                 on_hosted_event, nullptr) != ESP_OK) {
    ESP_LOGW(kTag, "Could not watch the WiFi chip (C6)");
    vTaskDelete(nullptr);
  }
  const esp_err_t err = esp_hosted_configure_heartbeat(true, kHostedBeatS);
  if (err != ESP_OK) {
    // No heartbeat will come: do not wait for one.
    esp_event_handler_unregister(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                                 on_hosted_event);
    esp_timer_stop(hosted_timer);
    ESP_LOGW(kTag, "WiFi chip (C6) heartbeat not available (%s); not watched",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(kTag,
             "Watching the WiFi chip (C6): a heartbeat every %d s, restart "
             "after %u s without one",
             kHostedBeatS, (unsigned)(kHostedSilenceMs / 1000));
  }
  vTaskDelete(nullptr);
}

#endif  // CONFIG_ESP_HOSTED_ENABLED

}  // namespace

void link_watch_before_wifi() {
  // Power save off (Arduino applies it when the station starts): a sleeping
  // station gets the frames its access point held back in bursts, late. The
  // gauges run on boat power.
  WiFi.setSleep(false);
}

void link_watch_begin() {
  sensesp::event_loop()->onRepeat(10000, []() {
    if (sk_stall_check(&sk_watch, WiFi.isConnected(),
                       sk_data().server_connected(), millis())) {
      restart("Signal K unreachable for 5 min with WiFi up");
    }
  });
#if CONFIG_ESP_HOSTED_ENABLED
  xTaskCreate(hosted_setup_task, "c6_watch", 4096, nullptr, 1, nullptr);
#endif
}

}  // namespace skgauges
