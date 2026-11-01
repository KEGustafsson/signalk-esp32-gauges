// Only a real JSON number counts as a reading (sk_parse.h), checked against
// ArduinoJson itself.

#include <ArduinoJson.h>

#include "sk_parse.h"
#include "test.h"

using namespace skgauges;

namespace {

JsonDocument parse(const char* json) {
  JsonDocument doc;
  deserializeJson(doc, json);
  return doc;
}

}  // namespace

TEST(numbers_are_readings) {
  float v = 0;
  CHECK(sk_number(parse("{\"value\":41.5}")["value"], &v));
  CHECK_NEAR(v, 41.5, 1e-6);
  CHECK(sk_number(parse("{\"value\":3}")["value"], &v));
  CHECK_NEAR(v, 3, 0);
  CHECK(sk_number(parse("{\"value\":-0.25}")["value"], &v));
  CHECK_NEAR(v, -0.25, 1e-6);
}

TEST(null_and_non_numbers_are_not_readings) {
  // ArduinoJson's as<float>() would turn every one of these into 0.
  const char* not_numbers[] = {
      "{\"value\":null}",     "{\"value\":\"12\"}", "{\"value\":true}",
      "{\"value\":{\"a\":1}}", "{\"value\":[1]}",   "{}",
  };
  for (const char* json : not_numbers) {
    float v = 123;
    CHECK(!sk_number(parse(json)["value"], &v));
    CHECK(v == 123);  // Untouched
  }
}

TEST(attitude_needs_roll) {
  float roll = 0, pitch = 0;
  CHECK(sk_attitude(
      parse("{\"value\":{\"roll\":0.1,\"pitch\":-0.02,\"yaw\":1}}")["value"],
      &roll, &pitch));
  CHECK_NEAR(roll, 0.1, 1e-6);
  CHECK_NEAR(pitch, -0.02, 1e-6);
  // Heel-only sources: pitch missing or null.
  CHECK(sk_attitude(parse("{\"value\":{\"roll\":0.2}}")["value"], &roll,
                    &pitch));
  CHECK(isnan(pitch));
  CHECK(sk_attitude(parse("{\"value\":{\"roll\":0.2,\"pitch\":null}}")["value"],
                    &roll, &pitch));
  CHECK(isnan(pitch));
  // No usable roll: not a reading.
  CHECK(!sk_attitude(parse("{\"value\":{\"roll\":null,\"pitch\":0}}")["value"],
                     &roll, &pitch));
  CHECK(!sk_attitude(parse("{\"value\":{\"pitch\":0}}")["value"], &roll,
                     &pitch));
  CHECK(!sk_attitude(parse("{\"value\":null}")["value"], &roll, &pitch));
  CHECK(!sk_attitude(parse("{\"value\":0.5}")["value"], &roll, &pitch));
}

TEST(meta_units_only_from_a_string) {
  CHECK(sk_meta_units(parse("{\"value\":{\"units\":\"Hz\"}}")["value"]) ==
        "Hz");
  // What SensESP's as<String>() would read as "null" (seen from a real
  // server for navigation.attitude), or as "12"/"true".
  const char* no_units[] = {
      "{\"value\":{\"description\":\"Vessel attitude\"}}",
      "{\"value\":{\"units\":null}}",
      "{\"value\":{\"units\":12}}",
      "{\"value\":{\"units\":true}}",
      "{\"value\":null}",
      "{}",
  };
  for (const char* json : no_units) {
    CHECK(sk_meta_units(parse(json)["value"]).empty());
  }
}

TEST(meta_deltas_merge_like_the_server) {
  // What the server sends at subscribe: its merged metadata for the path.
  GaugeMeta meta;
  sk_merge_meta(parse(R"({"value":{"units":"Hz","description":"Revs",
      "displayScale":{"lower":0,"upper":50},
      "zones":[{"lower":36.7,"upper":50,"state":"warn"},
               {"upper":5,"state":"normal"}]}})")["value"],
                &meta);
  CHECK(meta.units == "Hz");
  CHECK(meta.has_scale);
  CHECK_NEAR(meta.scale_upper, 50, 0);
  CHECK(meta.zones.size() == 2);
  CHECK(meta.zones[0].has_lower && meta.zones[0].has_upper);
  CHECK_NEAR(meta.zones[0].lower, 36.7, 1e-5);
  CHECK(meta.zones[0].color != 0);
  CHECK(!meta.zones[1].has_lower);
  CHECK(meta.zones[1].color == 0);  // normal: not drawn

  // What a SensESP source sends for its output at every connect, forwarded
  // as is: the zones and scale stay.
  sk_merge_meta(
      parse(R"({"value":{"units":"Hz","description":"Revs"}})")["value"],
      &meta);
  CHECK(meta.units == "Hz");
  CHECK(meta.has_scale);
  CHECK(meta.zones.size() == 2);

  // A field the delta carries replaces the old one.
  sk_merge_meta(parse(R"({"value":{"zones":[{"lower":40,"state":"alarm"}],
      "displayScale":{"lower":0,"upper":60}}})")["value"],
                &meta);
  CHECK(meta.zones.size() == 1);
  CHECK(meta.zones[0].has_lower && !meta.zones[0].has_upper);
  CHECK_NEAR(meta.scale_upper, 60, 0);
  CHECK(meta.units == "Hz");

  // Cleared: an empty list, null, or a malformed scale.
  sk_merge_meta(parse(R"({"value":{"zones":[]}})")["value"], &meta);
  CHECK(meta.zones.empty());
  sk_merge_meta(parse(R"({"value":{"zones":[{"lower":1,"state":"warn"}]}})")
                    ["value"],
                &meta);
  CHECK(meta.zones.size() == 1);
  sk_merge_meta(parse(R"({"value":{"zones":null,"displayScale":null,
      "units":null}})")["value"],
                &meta);
  CHECK(meta.zones.empty());
  CHECK(!meta.has_scale);
  CHECK(meta.units.empty());
  sk_merge_meta(parse(R"({"value":{"displayScale":{"lower":0,"upper":60}}})")
                    ["value"],
                &meta);
  CHECK(meta.has_scale);
  sk_merge_meta(parse(R"({"value":{"displayScale":{"upper":60}}})")["value"],
                &meta);
  CHECK(!meta.has_scale);

  // Not an object: nothing changes.
  sk_merge_meta(parse(R"({"value":{"units":"K"}})")["value"], &meta);
  sk_merge_meta(parse(R"({"value":null})")["value"], &meta);
  sk_merge_meta(parse(R"({})")["value"], &meta);
  CHECK(meta.units == "K");
}

TEST(meta_zones_with_bad_bounds_are_left_out) {
  // as<float>() would read these bounds as 0: a zone over the wrong part of
  // the dial.
  GaugeMeta meta;
  sk_merge_meta(parse(R"({"value":{"zones":[
      {"lower":"80","upper":100,"state":"alarm"},
      {"lower":80,"upper":true,"state":"alarm"},
      {"lower":80,"upper":null,"state":"alarm"},
      {"lower":60,"upper":80,"state":"warn"}]}})")["value"],
                &meta);
  CHECK(meta.zones.size() == 2);
  CHECK(meta.zones[0].has_lower && !meta.zones[0].has_upper);
  CHECK_NEAR(meta.zones[1].lower, 60, 0);
}
