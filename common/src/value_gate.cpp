#include "value_gate.h"

#include <math.h>

namespace skgauges {

bool gate_sample(GaugeValue* slot, float value, float value2, uint32_t now,
                 const Connection& link) {
  // JSON cannot carry NaN/Inf, but a huge exponent parses to a finite float
  // that overflows once scaled -- so check the converted values.
  if (!isfinite(value) || (!isnan(value2) && !isfinite(value2))) {
    gate_invalid(slot, now);
    return false;
  }
  // Nothing counts while the link is down. A new connection is one SensESP
  // built (its client generation changes when it tears the old client down)
  // or one the WebSocket library re-established on the same client (the
  // epoch moved: any connection state change). The first sample per path on
  // a new connection opens the replay window.
  if (!link.up) {
    return false;
  }
  if (!slot->conn_known || slot->conn_gen != link.generation ||
      slot->conn_epoch != link.epoch) {
    slot->conn_known = true;
    slot->conn_gen = link.generation;
    slot->conn_epoch = link.epoch;
    slot->replaying = true;
    slot->replay_until_ms = now + kReplayWindowMs;
    return false;
  }
  if (slot->replaying) {
    // Compared only inside the window, so a long-lived connection never
    // meets the signed wrap of this difference (24.8 days).
    if ((int32_t)(now - slot->replay_until_ms) < 0) {
      return false;
    }
    slot->replaying = false;
  }
  slot->value = value;
  slot->value2 = value2;
  slot->updated_ms = now;
  slot->seen = true;
  slot->invalid_pending = false;
  return true;
}

void gate_invalid(GaugeValue* slot, uint32_t now) {
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

void gate_sweep(GaugeValue* slot, uint32_t now) {
  if (slot->seen && !slot->fresh(now)) {
    slot->seen = false;
  }
  if (slot->invalid_pending && now - slot->invalid_ms >= kInvalidGraceMs) {
    slot->invalid_pending = false;
    slot->seen = false;
  }
  // Close a window no later sample closed (a path that went quiet), so its
  // signed compare never meets the 24.8-day wrap.
  if (slot->replaying && (int32_t)(now - slot->replay_until_ms) >= 0) {
    slot->replaying = false;
  }
}

}  // namespace skgauges
