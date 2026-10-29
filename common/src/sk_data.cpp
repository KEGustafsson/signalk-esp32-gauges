#include "sk_data.h"

#include <Arduino.h>

#include <memory>

#include "sensesp/system/lambda_consumer.h"
#include "sensesp/ui/config_item.h"
#include "sensesp_app.h"
#include "sk_listeners.h"

namespace skgauges {

namespace {

// After a (re)connect, signalk-server replays the last value it holds for
// each path -- one per $source, however old -- in a burst, and SensESP drops
// the delta timestamps. Everything a path receives within this window after
// its first sample on a new connection is treated as replay and ignored.
constexpr uint32_t kReplayWindowMs = 1000;
// A null/non-numeric delta blanks the gauge unless another source delivered
// a real value within this time (several sources can publish one path).
constexpr uint32_t kInvalidGraceMs = 2000;

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
    auto on_invalid = [this, slot]() { invalid(slot); };

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
    invalid(slot);
    return;
  }
  // Replay guard. The client generation changes synchronously on the loop
  // when SensESP starts a new connection, and is_connected() is false until
  // it is up, so leftovers of an old connection never count as fresh.
  auto ws = sensesp::sensesp_app->get_ws_client();
  if (!ws->is_connected()) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t gen = ws->client_generation();
  if (slot->conn_gen != gen) {
    slot->conn_gen = gen;
    slot->replay_until_ms = now + kReplayWindowMs;
    return;
  }
  if ((int32_t)(now - slot->replay_until_ms) < 0) {
    return;
  }
  slot->value = value;
  slot->value2 = value2;
  slot->updated_ms = now;
  slot->seen = true;
}

void SkData::invalid(GaugeValue* slot) {
  if (millis() - slot->updated_ms >= kInvalidGraceMs) {
    slot->seen = false;
  }
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
