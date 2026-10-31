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
