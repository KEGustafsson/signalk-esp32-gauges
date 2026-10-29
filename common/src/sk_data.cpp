#include "sk_data.h"

#include <Arduino.h>

#include <memory>

#include "sensesp/system/lambda_consumer.h"
#include "sensesp/ui/config_item.h"
#include "sensesp_app.h"
#include "sk_listeners.h"

namespace skgauges {

namespace {

float wrap360(float deg) {
  deg = fmodf(deg, 360.0f);
  return deg < 0.0f ? deg + 360.0f : deg;
}
}  // namespace

SkData& sk_data() {
  static SkData instance;
  return instance;
}

void SkData::begin(Side unit_side) {
  const size_t count = kGaugeCount < kMaxGauges ? kGaugeCount : kMaxGauges;

  // signalk-server answers a subscription by replaying the last value it holds
  // for each path, however old -- and SensESP drops the delta timestamp. So
  // the first sample of every path after each (re)connect is ignored: every
  // connection state change starts a new generation, and store() only accepts
  // a slot's second sample within a generation. A live source loses one
  // sample; a dead one stays dashes instead of showing its last reading.
  sensesp::sensesp_app->get_ws_client()->connect_to(
      new sensesp::LambdaConsumer<sensesp::SKWSConnectionState>(
          [this](sensesp::SKWSConnectionState) { conn_gen_++; }));

  // Drop values that have gone stale, so `seen` never outlives them.
  sensesp::event_loop()->onRepeat(1000, [this]() {
    const uint32_t now = millis();
    for (GaugeValue& slot : values_) {
      if (slot.seen && !slot.fresh(now)) {
        slot.seen = false;
      }
    }
  });

  for (size_t i = 0; i < count; i++) {
    const GaugeDef& def = kGauges[i];
    if (!shown_on(def, unit_side)) {
      continue;
    }
    GaugeValue* slot = &values_[i];
    String config_path = String("/Gauges/") + def.id;
    String title = String(def.title) + " (" + def.id + ")";
    const float scale = def.scale;
    const float offset = def.offset;
    auto on_invalid = [slot]() { slot->seen = false; };

    if (def.kind == GaugeKind::kAttitude) {
      auto listener = std::make_shared<AttitudeListener>(
          def.sk_path, def.listen_delay_ms, config_path,
          [this, slot, scale, offset](float roll_rad, float pitch_rad) {
            store(slot, roll_rad * scale + offset,
                  pitch_rad * scale + offset);
          });
      listener->set_invalid_callback(on_invalid);
      paths_[i] = [listener]() { return listener->sk_path_copy(); };
      sensesp::ConfigItem(listener)
          ->set_title(title)
          ->set_requires_restart(true)
          ->set_sort_order(100 + i);
      continue;
    }

    auto listener = std::make_shared<StrictFloatListener>(
        def.sk_path, def.listen_delay_ms, config_path);
    const bool is_angle = def.kind == GaugeKind::kCompass;
    listener->set_invalid_callback(on_invalid);
    listener->connect_to(new sensesp::LambdaConsumer<float>(
        [this, slot, scale, offset, is_angle](float raw) {
          const float value = raw * scale + offset;
          store(slot, is_angle ? wrap360(value) : value, NAN);
        }));
    paths_[i] = [listener]() { return listener->sk_path_copy(); };
    sensesp::ConfigItem(listener)
        ->set_title(title)
        ->set_requires_restart(true)
        ->set_sort_order(100 + i);
  }
}

void SkData::store(GaugeValue* slot, float value, float value2) {
  // JSON cannot carry NaN/Inf, but a huge exponent parses to a finite float
  // that overflows once scaled -- so check the converted values.
  if (!isfinite(value) || (!isnan(value2) && !isfinite(value2))) {
    slot->seen = false;
    return;
  }
  if (slot->conn_gen != conn_gen_) {
    slot->conn_gen = conn_gen_;  // Possibly the server's cached replay
    return;
  }
  slot->value = value;
  slot->value2 = value2;
  slot->updated_ms = millis();
  slot->seen = true;
}

String SkData::sk_path(size_t index) const {
  if (index >= kMaxGauges || !paths_[index]) {
    return String();
  }
  return paths_[index]();
}

bool SkData::server_connected() const {
  if (sensesp::sensesp_app == nullptr) {
    return false;
  }
  auto ws = sensesp::sensesp_app->get_ws_client();
  return ws != nullptr && ws->is_connected();
}

}  // namespace skgauges
