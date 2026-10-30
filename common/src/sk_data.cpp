#include "sk_data.h"

#include <Arduino.h>

#include <memory>

#include "gauge_settings.h"
#include "sensesp/system/lambda_consumer.h"
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
      if (slot.seen && !slot.fresh(now)) {
        slot.seen = false;
      }
      if (slot.invalid_pending &&
          now - slot.invalid_ms >= kInvalidGraceMs) {
        slot.invalid_pending = false;
        slot.seen = false;
      }
      // Close a window no later sample closed (a path that went quiet), so
      // its signed compare never meets the 24.8-day wrap.
      if (slot.replaying && (int32_t)(now - slot.replay_until_ms) >= 0) {
        slot.replaying = false;
      }
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
  // JSON cannot carry NaN/Inf, but a huge exponent parses to a finite float
  // that overflows once scaled -- so check the converted values.
  if (!isfinite(value) || (!isnan(value2) && !isfinite(value2))) {
    invalid(slot);
    return;
  }
  // Replay guard. is_connected() follows the WebSocket task directly, so
  // nothing counts while the link is down. A new connection is one SensESP
  // built (its client generation changes when it tears the old client down)
  // or one the WebSocket library re-established on the same client
  // (conn_epoch_ moved: any connection state change). The first sample per
  // path on a new connection opens the replay window.
  auto ws = sensesp::sensesp_app->get_ws_client();
  if (!ws->is_connected()) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t gen = ws->client_generation();
  if (!slot->conn_known || slot->conn_gen != gen ||
      slot->conn_epoch != conn_epoch_) {
    slot->conn_known = true;
    slot->conn_gen = gen;
    slot->conn_epoch = conn_epoch_;
    slot->replaying = true;
    slot->replay_until_ms = now + kReplayWindowMs;
    return;
  }
  if (slot->replaying) {
    // Compared only inside the window, so a long-lived connection never
    // meets the signed wrap of this difference (24.8 days).
    if ((int32_t)(now - slot->replay_until_ms) < 0) {
      return;
    }
    slot->replaying = false;
  }
  slot->value = value;
  slot->value2 = value2;
  slot->updated_ms = now;
  slot->seen = true;
  slot->invalid_pending = false;
}

void SkData::invalid(GaugeValue* slot) {
  const uint32_t now = millis();
  if (!slot->seen || now - slot->updated_ms >= kInvalidGraceMs) {
    slot->seen = false;
    slot->invalid_pending = false;
    return;
  }
  // A valid value arrived moments ago, perhaps from another source of the
  // same path: blank only if none follows within the grace time.
  if (!slot->invalid_pending) {
    slot->invalid_pending = true;
    slot->invalid_ms = now;
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
