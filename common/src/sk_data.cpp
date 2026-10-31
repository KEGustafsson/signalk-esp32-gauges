#include "sk_data.h"

#include <Arduino.h>

#include <memory>

#include "gauge_settings.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp_app.h"
#include "sk_listeners.h"
#include "value_gate.h"

namespace skgauges {

namespace {

float wrap360(float deg) {
  deg = fmodf(deg, 360.0f);
  if (deg < 0.0f) deg += 360.0f;
  return deg >= 360.0f ? 0.0f : deg;  // -1e-7 + 360 rounds to 360
}
}  // namespace

SkData& sk_data() {
  static SkData instance;
  return instance;
}

void SkData::begin() {
  // Every state change counts, the disconnect included: the "connected"
  // state and the replayed deltas reach the loop through separate polls, in
  // no fixed order, but a disconnect is seen long before the WebSocket
  // library's own reconnect (10 s later).
  sensesp::sensesp_app->get_ws_client()->connect_to(
      new sensesp::LambdaConsumer<sensesp::SKWSConnectionState>(
          [this](sensesp::SKWSConnectionState) { conn_epoch_++; }));

  // Drop values that have gone stale, so `seen` never outlives them, and
  // values a null disowned that no other source has confirmed since.
  sensesp::event_loop()->onRepeat(1000, [this]() {
    const uint32_t now = millis();
    for (GaugeValue& slot : values_) {
      gate_sweep(&slot, now);
    }
  });

  // The pages gauge_ui shows, each with its path, display unit and data
  // timeout from its settings card. Metadata for the path (zones,
  // displayScale) goes to gauge_config(), which redraws the page from it.
  for (size_t i : gauge_config().pages()) {
    const GaugeDef& def = gauge_config().def(i);
    GaugeValue* slot = &values_[i];
    slot->stale_ms = gauge_config().stale_ms(i);
    if (def.sk_path == nullptr || def.sk_path[0] == '\0') {
      continue;  // A dial with no path yet: nothing to read, it shows dashes
    }
    const String path = def.sk_path;
    const float scale = def.scale;
    const float offset = def.offset;
    auto on_invalid = [this, slot]() { invalid(slot); };
    auto on_meta = [i](const sensesp::SKMetaView& meta) {
      gauge_config().set_meta(i, meta);
    };

    if (def.kind == GaugeKind::kAttitude) {
      auto listener = std::make_shared<AttitudeListener>(
          path, def.listen_delay_ms,
          [this, slot, scale, offset](float roll_rad, float pitch_rad) {
            store(slot, roll_rad * scale + offset,
                  pitch_rad * scale + offset);
          });
      listener->set_invalid_callback(on_invalid);
      listener->set_meta_callback(on_meta);
      listeners_.push_back(listener);
      continue;
    }

    auto listener =
        std::make_shared<StrictFloatListener>(path, def.listen_delay_ms);
    const bool is_angle = def.kind == GaugeKind::kCompass;
    listener->set_invalid_callback(on_invalid);
    listener->set_meta_callback(on_meta);
    listener->connect_to(new sensesp::LambdaConsumer<float>(
        [this, slot, scale, offset, is_angle](float raw) {
          // A heading beyond two turns is garbage, not a bearing to wrap.
          if (is_angle && fabsf(raw * scale) > 720.0f) {
            invalid(slot);
            return;
          }
          const float value = raw * scale + offset;
          store(slot, is_angle ? wrap360(value) : value, NAN);
        }));
    listeners_.push_back(listener);
  }
}

void SkData::store(GaugeValue* slot, float value, float value2) {
  auto ws = sensesp::sensesp_app->get_ws_client();
  Connection link;
  // is_connected() follows the WebSocket task directly, so nothing counts
  // while the link is down.
  link.up = ws->is_connected();
  link.generation = ws->client_generation();
  link.epoch = conn_epoch_;
  gate_sample(slot, value, value2, millis(), link);
}

void SkData::invalid(GaugeValue* slot) { gate_invalid(slot, millis()); }

bool SkData::server_connected() const {
  if (sensesp::sensesp_app == nullptr) {
    return false;
  }
  auto ws = sensesp::sensesp_app->get_ws_client();
  return ws != nullptr && ws->is_connected();
}

}  // namespace skgauges
