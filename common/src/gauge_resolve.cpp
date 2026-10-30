#include "gauge_resolve.h"

#include <math.h>
#include <string.h>

#include <initializer_list>

#include "units.h"

namespace skgauges {

namespace {

// Converts a value in `from` to `to` through the SI unit they share.
float convert(float value, const DisplayUnit& from, const DisplayUnit& to) {
  const float si = (value - from.offset) / from.scale;
  return si * to.scale + to.offset;
}

bool near(float a, float b) {
  return fabsf(a - b) <= 1e-4f * fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
}

// A bound converted from SI, cleaned of float noise (15.4333 m/s -> 30 kn,
// not 29.9999).
float tidy(float value) {
  return fabsf(value) < 1e6f ? roundf(value * 1000.0f) / 1000.0f : value;
}

}  // namespace

uint32_t zone_state_color(const char* state) {
  if (state == nullptr) return 0;
  // SKip's dark theme (src/themes/_m3dark.scss).
  if (strcmp(state, "nominal") == 0) return 0x0164FA;
  if (strcmp(state, "alert") == 0) return 0xFFFF60;
  if (strcmp(state, "warn") == 0) return 0xFF8D41;
  if (strcmp(state, "alarm") == 0) return 0xDD0000;
  if (strcmp(state, "emergency") == 0) return 0xFF0000;
  return 0;  // "normal" and unknown states are not drawn
}

float nice_step(float min, float max, uint8_t* minor_per_major) {
  const float range = max - min;
  static const float kMantissa[] = {1.0f, 2.0f, 2.5f, 5.0f};
  static const uint8_t kMinors[] = {5, 4, 5, 5};
  if (range > 0.0f && isfinite(range)) {
    float decade = powf(10.0f, floorf(log10f(range / 10.0f)));
    for (int round = 0; round < 3; round++, decade *= 10.0f) {
      for (size_t i = 0; i < 4; i++) {
        const float step = kMantissa[i] * decade;
        const float count = range / step;
        if (count < 4.0f - 1e-4f || count > 10.0f + 1e-4f) continue;
        if (!near(count, roundf(count))) continue;
        // Majors must also land on whole multiples of the step.
        if (!near(min / step, roundf(min / step))) continue;
        if (minor_per_major != nullptr) *minor_per_major = kMinors[i];
        return step;
      }
    }
  }
  // An awkward range (e.g. 3..17): five equal parts.
  if (minor_per_major != nullptr) *minor_per_major = 4;
  return range > 0.0f ? range / 5.0f : 1.0f;
}

void nice_range(float* min, float* max) {
  const float range = *max - *min;
  if (!(range > 0.0f) || !isfinite(range)) return;
  const float raw = range / 6.0f;
  const float decade = powf(10.0f, floorf(log10f(raw)));
  float step = 10.0f * decade;
  for (float m : {1.0f, 2.0f, 2.5f, 5.0f}) {
    if (m * decade >= raw * (1.0f - 1e-4f)) {
      step = m * decade;
      break;
    }
  }
  *min = floorf(*min / step + 1e-4f) * step;
  *max = ceilf(*max / step - 1e-4f) * step;
}

GaugeDef resolve_gauge(const GaugeDef& base, const GaugeOptions& options,
                       const GaugeMeta& meta) {
  GaugeDef def = base;
  if (options.title != nullptr && options.title[0] != '\0') {
    def.title = options.title;
  }
  if (options.sk_path != nullptr && options.sk_path[0] != '\0') {
    def.sk_path = options.sk_path;
  }

  const DisplayUnit* base_unit = find_display_unit(base.unit_id);
  const DisplayUnit* unit = base_unit;
  // Any unit, as in SKip (the path may have been pointed at another
  // quantity); heading and attitude are always in degrees.
  if (base.kind == GaugeKind::kRadial) {
    const DisplayUnit* chosen = find_display_unit(options.unit_id);
    if (chosen != nullptr) {
      unit = chosen;
    }
  }
  if (unit != nullptr) {
    def.unit_id = unit->id;
    def.unit = unit->label;
    def.scale = unit->scale;
    def.offset = unit->offset;
    if (is_as_sent(*unit) && meta.units != "ratio") {
      def.unit = meta.units.c_str();  // "" until the server says
    }
  }
  if (base.kind == GaugeKind::kCompass) {
    def.unit = unit_for_path(base, def.sk_path);
  }
  if (options.decimals >= 0 && options.decimals <= 3) {
    def.decimals = (uint8_t)options.decimals;
  }

  if (base.kind != GaugeKind::kRadial) {
    return def;  // Fixed scales; the attitude page keeps its heel bands
  }

  // Scale: by hand, else the server's displayScale, else the default --
  // converted when the unit measures the same quantity, else 0..100 (SKip's
  // fallback).
  float min = base.min;
  float max = base.max;
  if (unit != nullptr && base_unit != nullptr && unit != base_unit) {
    if (strcmp(unit->si, base_unit->si) == 0) {
      min = convert(base.min, *base_unit, *unit);
      max = convert(base.max, *base_unit, *unit);
      nice_range(&min, &max);
    } else {
      min = 0.0f;
      max = 100.0f;
    }
  }
  if (options.manual_scale && isfinite(options.min) &&
      isfinite(options.max) && options.min < options.max) {
    min = options.min;
    max = options.max;
  } else if (meta.has_scale && unit != nullptr) {
    const float lower = tidy(meta.scale_lower * unit->scale + unit->offset);
    const float upper = tidy(meta.scale_upper * unit->scale + unit->offset);
    if (isfinite(lower) && isfinite(upper) && lower < upper) {
      min = lower;
      max = upper;
    }
  }
  def.min = min;
  def.max = max;
  if (!(unit == base_unit && near(min, base.min) && near(max, base.max))) {
    def.major_step = nice_step(min, max, &def.minor_per_major);
  }

  // Zones from the server, in the display unit, clipped to the dial.
  def.zone_count = 0;
  if (options.show_zones && unit != nullptr) {
    for (const SiZone& z : meta.zones) {
      if (z.color == 0 || def.zone_count >= kMaxZones) continue;
      float from =
          z.has_lower ? tidy(z.lower * unit->scale + unit->offset) : min;
      float to = z.has_upper ? tidy(z.upper * unit->scale + unit->offset) : max;
      if (!isfinite(from) || !isfinite(to)) continue;
      if (from < min) from = min;
      if (to > max) to = max;
      if (from >= to) continue;  // Outside the dial, or empty
      def.zones[def.zone_count++] = {from, to, z.color};
    }
  }
  return def;
}

}  // namespace skgauges
