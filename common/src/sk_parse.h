#pragma once

// How a Signal K delta's value is read: only a real JSON number counts.
// ArduinoJson's as<float>() coerces null, a string or an object to 0.0f --
// for a gauge that is a confident lie (a source publishing `null` because it
// lost its data would read 0 rpm). Header-only and free of SensESP, so the
// rules are unit-tested on the host (tests/) against ArduinoJson itself.

#include <ArduinoJson.h>
#include <math.h>

#include <string>

#include "gauge_resolve.h"

namespace skgauges {

// A numeric value: true and *out set, or false for null, a string, a
// boolean, an object or a missing value.
inline bool sk_number(JsonVariantConst value, float* out) {
  if (!value.is<float>()) {
    return false;
  }
  *out = value.as<float>();
  return true;
}

// navigation.attitude, an object {roll, pitch, yaw} in radians; any axis may
// be missing (NMEA 2000 PGN 127257 leaves out what the sender does not
// measure). True when roll is a number; *pitch is NAN if it is not. Yaw is
// not used.
inline bool sk_attitude(JsonVariantConst value, float* roll, float* pitch) {
  if (!sk_number(value["roll"], roll)) {
    return false;
  }
  if (!sk_number(value["pitch"], pitch)) {
    *pitch = NAN;
  }
  return true;
}

// The unit of a path from its metadata object (a meta delta's `value`), or
// "" when the server gives none. SensESP's SKMetaView::units reads it with
// as<String>(), which turns a missing unit into the text "null" -- a unit
// that matches nothing (navigation.attitude has none).
inline std::string sk_meta_units(JsonVariantConst meta) {
  JsonVariantConst units = meta["units"];
  return units.is<const char*>() ? units.as<const char*>() : "";
}

// Applies a meta delta's `value` to what is known of the path, the way the
// Signal K server merges it: a field the delta carries replaces the old one
// (null or a malformed one clears it), a field it leaves out is kept. Each
// source's metadata reaches the gauge as sent, not merged: SensESP, for one,
// sends {units, description} for its outputs at every connect, and taking
// that for the whole metadata wiped the server's zones minutes after boot.
// A zone with a bound that is not a number is left out (as<float>() would
// read it as 0).
inline void sk_merge_meta(JsonVariantConst value, GaugeMeta* meta) {
  if (!value.is<JsonObjectConst>()) {
    return;
  }
  if (!value["units"].isUnbound()) {
    meta->units = sk_meta_units(value);
  }
  JsonVariantConst scale = value["displayScale"];
  if (!scale.isUnbound()) {
    meta->has_scale =
        scale["lower"].is<float>() && scale["upper"].is<float>();
    meta->scale_lower = meta->has_scale ? scale["lower"].as<float>() : 0.0f;
    meta->scale_upper = meta->has_scale ? scale["upper"].as<float>() : 0.0f;
  }
  JsonVariantConst zones = value["zones"];
  if (!zones.isUnbound()) {
    meta->zones.clear();
    for (JsonVariantConst z : zones.as<JsonArrayConst>()) {
      SiZone zone;
      JsonVariantConst lower = z["lower"];
      JsonVariantConst upper = z["upper"];
      if ((!lower.isNull() && !lower.is<float>()) ||
          (!upper.isNull() && !upper.is<float>())) {
        continue;
      }
      zone.has_lower = lower.is<float>();
      zone.lower = zone.has_lower ? lower.as<float>() : 0.0f;
      zone.has_upper = upper.is<float>();
      zone.upper = zone.has_upper ? upper.as<float>() : 0.0f;
      JsonVariantConst state = z["state"];
      zone.color = zone_state_color(
          state.is<const char*>() ? state.as<const char*>() : "");
      meta->zones.push_back(zone);
    }
  }
}

}  // namespace skgauges
