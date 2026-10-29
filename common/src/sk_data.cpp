#include "sk_data.h"

#include <Arduino.h>

#include <memory>

#include "sensesp/system/lambda_consumer.h"
#include "sensesp/ui/config_item.h"
#include "sensesp_app.h"
#include "sk_listeners.h"

namespace skgauges {

namespace {
constexpr float kRadToDeg = 57.29577951308232f;

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

  for (size_t i = 0; i < count; i++) {
    const GaugeDef& def = kGauges[i];
    if (!shown_on(def, unit_side)) {
      continue;
    }
    GaugeValue* slot = &values_[i];
    String config_path = String("/Gauges/") + def.id;
    String title = String(def.title) + " (" + def.id + ")";

    if (def.kind == GaugeKind::kAttitude) {
      auto listener = std::make_shared<AttitudeListener>(
          def.sk_path, def.listen_delay_ms, config_path,
          [slot](float roll_rad, float pitch_rad) {
            if (!isfinite(roll_rad) || !isfinite(pitch_rad)) {
              return;
            }
            slot->value = roll_rad * kRadToDeg;
            slot->value2 = pitch_rad * kRadToDeg;
            slot->updated_ms = millis();
            slot->seen = true;
          });
      sensesp::ConfigItem(listener)
          ->set_title(title)
          ->set_requires_restart(true)
          ->set_sort_order(100 + i);
      continue;
    }

    auto listener = std::make_shared<StrictFloatListener>(
        def.sk_path, def.listen_delay_ms, config_path);
    const bool is_angle = def.kind == GaugeKind::kCompass;
    const float scale = def.scale;
    const float offset = def.offset;
    listener->connect_to(
        new sensesp::LambdaConsumer<float>([slot, scale, offset, is_angle](
                                               float raw) {
          // JSON cannot carry NaN/Inf, but a huge exponent parses to Inf.
          if (!isfinite(raw)) {
            return;
          }
          float value = raw * scale + offset;
          slot->value = is_angle ? wrap360(value) : value;
          slot->updated_ms = millis();
          slot->seen = true;
        }));
    sensesp::ConfigItem(listener)
        ->set_title(title)
        ->set_requires_restart(true)
        ->set_sort_order(100 + i);
  }
}

bool SkData::server_connected() const {
  if (sensesp::sensesp_app == nullptr) {
    return false;
  }
  auto ws = sensesp::sensesp_app->get_ws_client();
  return ws != nullptr && ws->is_connected();
}

}  // namespace skgauges
