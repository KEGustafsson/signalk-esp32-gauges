// Display units, scales and server zones (gauge_resolve.h, units.h).

#include <string.h>

#include "gauge_defs.h"
#include "gauge_resolve.h"
#include "test.h"
#include "units.h"

using namespace skgauges;

namespace {

SiZone zone(bool has_lower, float lower, bool has_upper, float upper,
            const char* state) {
  SiZone z;
  z.has_lower = has_lower;
  z.lower = lower;
  z.has_upper = has_upper;
  z.upper = upper;
  z.color = zone_state_color(state);
  return z;
}

const GaugeDef& factory(Side side, size_t slot) {
  return *default_page(side, slot);
}

}  // namespace

TEST(units_ids_are_unique_and_found) {
  for (size_t i = 0; i < kDisplayUnitCount; i++) {
    CHECK(find_display_unit(kDisplayUnits[i].id) == &kDisplayUnits[i]);
    for (size_t j = i + 1; j < kDisplayUnitCount; j++) {
      CHECK(strcmp(kDisplayUnits[i].id, kDisplayUnits[j].id) != 0);
    }
  }
  CHECK(find_display_unit("furlong") == nullptr);
  CHECK(find_display_unit(nullptr) == nullptr);
  CHECK(is_as_sent(*find_display_unit("SI")));
  CHECK(!is_as_sent(*find_display_unit("rpm")));
}

TEST(zone_states_take_skip_colours) {
  CHECK(zone_state_color("nominal") == 0x0164FA);
  CHECK(zone_state_color("alert") == 0xFFFF60);
  CHECK(zone_state_color("warn") == 0xFF8D41);
  CHECK(zone_state_color("alarm") == 0xDD0000);
  CHECK(zone_state_color("emergency") == 0xFF0000);
  CHECK(zone_state_color("normal") == 0);  // Not drawn
  CHECK(zone_state_color("bogus") == 0);
  CHECK(zone_state_color(nullptr) == 0);
}

TEST(nice_step_divides_the_range) {
  uint8_t minor = 0;
  CHECK_NEAR(nice_step(0, 3000, &minor), 500, 1e-3);
  CHECK(minor == 5);
  CHECK_NEAR(nice_step(0, 30, &minor), 5, 1e-6);
  CHECK(minor == 5);
  CHECK_NEAR(nice_step(0, 120, &minor), 20, 1e-6);
  CHECK(minor == 4);
  CHECK_NEAR(nice_step(10, 16, &minor), 1, 1e-6);
  // An awkward range: five equal parts.
  CHECK_NEAR(nice_step(3, 17, &minor), 14.0 / 5.0, 1e-5);
  CHECK(minor == 4);
}

TEST(nice_range_rounds_outward) {
  float lo = 32, hi = 248;
  nice_range(&lo, &hi);
  CHECK_NEAR(lo, 0, 1e-4);
  CHECK_NEAR(hi, 250, 1e-4);
}

TEST(convert_range_between_units) {
  struct {
    const char* from;
    const char* to;
    float lo, hi, want_lo, want_hi;
  } cases[] = {
      {"\xC2\xB0" "C", "\xC2\xB0" "F", 0, 120, 0, 250},
      {"rpm", "Hz", 0, 3000, 0, 50},
      {"kn", "km/h", 0, 30, 0, 60},
      {"bar", "psi", 0, 10, 0, 150},
      {"SI", "rpm", 0, 50, 0, 3000},   // SI counts as the other's SI unit
      {"rpm", "SI", 0, 3000, 0, 50},
      {"rpm", "\xC2\xB0" "C", 0, 3000, 0, 100},  // Another quantity
      {"rpm", "rpm", 0, 3000, 0, 3000},           // Same unit
      {"rpm", "furlong", 0, 3000, 0, 3000},       // Unknown: unchanged
  };
  for (const auto& c : cases) {
    float lo = c.lo, hi = c.hi;
    convert_range(c.from, c.to, &lo, &hi);
    CHECK_NEAR(lo, c.want_lo, 1e-3);
    CHECK_NEAR(hi, c.want_hi, 1e-3);
  }
}

TEST(server_zones_in_display_units_clipped_to_the_dial) {
  GaugeMeta meta;
  meta.units = "Hz";
  meta.zones = {zone(true, 0, true, 41.667f, "normal"),  // Not drawn
                zone(true, 41.667f, true, 46.667f, "warn"),
                zone(true, 46.667f, false, 0, "alarm"),  // Open: to the top
                zone(true, 60, false, 0, "emergency")};  // Beyond the dial
  const GaugeDef def =
      resolve_gauge(factory(Side::kPort, 0), GaugeOptions(), meta);
  CHECK(def.zone_count == 2);
  CHECK_NEAR(def.zones[0].from, 2500, 0.1);
  CHECK_NEAR(def.zones[0].to, 2800, 0.1);
  CHECK(def.zones[0].color == 0xFF8D41);
  CHECK_NEAR(def.zones[1].from, 2800, 0.1);
  CHECK_NEAR(def.zones[1].to, 3000, 0.1);
  CHECK(def.zones[1].color == 0xDD0000);
}

TEST(open_lower_zone_starts_at_the_dial_minimum) {
  GaugeMeta meta;
  meta.zones = {zone(false, 0, true, 11.8f, "alarm")};
  GaugeOptions options;
  options.unit_id = "V";
  options.manual_scale = true;
  options.min = 10;
  options.max = 16;
  const GaugeDef def =
      resolve_gauge(style_template(GaugeKind::kRadial), options, meta);
  CHECK(def.zone_count == 1);
  CHECK_NEAR(def.zones[0].from, 10, 1e-4);
  CHECK_NEAR(def.zones[0].to, 11.8, 1e-4);
}

TEST(zones_are_capped_and_can_be_switched_off) {
  GaugeMeta meta;
  for (int i = 0; i < 12; i++) {
    meta.zones.push_back(zone(true, i * 10.0f, true, i * 10.0f + 5, "warn"));
  }
  GaugeOptions options;
  options.manual_scale = true;
  options.min = 0;
  options.max = 200;
  GaugeDef def = resolve_gauge(style_template(GaugeKind::kRadial), options,
                               meta);
  CHECK(def.zone_count == kMaxZones);
  options.show_zones = false;
  def = resolve_gauge(style_template(GaugeKind::kRadial), options, meta);
  CHECK(def.zone_count == 0);
}

TEST(display_scale_from_the_server_and_by_hand) {
  GaugeMeta meta;
  meta.has_scale = true;  // 0..15.4333 m/s = 0..30 kn, without float noise
  meta.scale_lower = 0;
  meta.scale_upper = 15.4333f;
  GaugeDef def = resolve_gauge(factory(Side::kStarboard, 1), GaugeOptions(),
                               meta);
  CHECK_NEAR(def.min, 0, 1e-6);
  CHECK_NEAR(def.max, 30, 1e-2);
  // By hand wins over the server.
  GaugeOptions options;
  options.manual_scale = true;
  options.min = 0;
  options.max = 40;
  def = resolve_gauge(factory(Side::kStarboard, 1), options, meta);
  CHECK_NEAR(def.max, 40, 1e-6);
  CHECK_NEAR(def.major_step, 5, 1e-6);  // Ticks follow the range
}

TEST(unit_conversion_of_the_reading) {
  GaugeOptions options;
  options.unit_id = "\xC2\xB0" "F";
  const GaugeDef def =
      resolve_gauge(factory(Side::kPort, 3), options, GaugeMeta());
  // 373.15 K = 212 F
  CHECK_NEAR(373.15 * def.scale + def.offset, 212, 1e-2);
  CHECK(strcmp(def.unit, "\xC2\xB0" "F") == 0);
}

TEST(si_dial_is_labelled_from_the_server) {
  GaugeMeta meta;
  meta.units = "V";
  GaugeDef def =
      resolve_gauge(style_template(GaugeKind::kRadial), GaugeOptions(), meta);
  CHECK(strcmp(def.unit, "V") == 0);
  CHECK_NEAR(def.scale, 1, 0);
  meta.units = "ratio";  // A ratio has no label
  def = resolve_gauge(style_template(GaugeKind::kRadial), GaugeOptions(), meta);
  CHECK(strcmp(def.unit, "") == 0);
}

TEST(compass_label_follows_the_path) {
  const GaugeDef& heading = style_template(GaugeKind::kCompass);
  GaugeDef def = resolve_gauge(heading, GaugeOptions(), GaugeMeta());
  CHECK(strcmp(def.unit, "\xC2\xB0T") == 0);
  GaugeOptions options;
  options.sk_path = "navigation.headingMagnetic";
  def = resolve_gauge(heading, options, GaugeMeta());
  CHECK(strcmp(def.unit, "\xC2\xB0M") == 0);
  CHECK(strcmp(def.sk_path, "navigation.headingMagnetic") == 0);
}

TEST(attitude_keeps_its_scale_and_bands) {
  GaugeOptions options;
  options.unit_id = "rpm";  // Ignored: degrees only
  options.manual_scale = true;
  options.min = 0;
  options.max = 1;
  GaugeMeta meta;
  meta.zones = {zone(true, 0, true, 1, "alarm")};
  const GaugeDef def =
      resolve_gauge(style_template(GaugeKind::kAttitude), options, meta);
  CHECK_NEAR(def.min, -40, 0);
  CHECK_NEAR(def.max, 40, 0);
  CHECK(def.zone_count == 3);  // The heel bands, not the server's zone
  CHECK_NEAR(def.scale, 57.29578, 1e-3);
}

TEST(title_and_decimals_from_the_card) {
  GaugeOptions options;
  options.title = "Fuel";
  options.decimals = 2;
  GaugeDef def = resolve_gauge(style_template(GaugeKind::kRadial), options,
                               GaugeMeta());
  CHECK(strcmp(def.title, "Fuel") == 0);
  CHECK(def.decimals == 2);
  options.title = "";  // Empty: the style's default
  options.decimals = 9;  // Out of range: the default's
  def = resolve_gauge(style_template(GaugeKind::kRadial), options,
                      GaugeMeta());
  CHECK(strcmp(def.title, "Gauge") == 0);
  CHECK(def.decimals == 1);
}
