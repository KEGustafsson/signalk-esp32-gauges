// A page card's contents, the rules a save follows and its schema
// (page_rules.h).

#include <ArduinoJson.h>

#include "gauge_defs.h"
#include "page_rules.h"
#include "test.h"

using namespace skgauges;

namespace {

PageFields coolant() { return factory_fields(default_page(Side::kPort, 3)); }

}  // namespace

TEST(factory_fields_of_a_page) {
  const PageFields rpm = factory_fields(default_page(Side::kPort, 0));
  CHECK(rpm.kind == GaugeKind::kRadial);
  CHECK(rpm.title == "PRT Engine");
  CHECK(rpm.sk_path == "propulsion.port.revolutions");
  CHECK(rpm.unit_id == "rpm");
  CHECK(rpm.min == 0 && rpm.max == 3000);
  CHECK(!rpm.manual_scale);
  // Past the factory pages: a dial with no path.
  const PageFields extra = factory_fields(nullptr);
  CHECK(extra.kind == GaugeKind::kRadial);
  CHECK(extra.title.empty() && extra.sk_path.empty());
  CHECK(extra.unit_id == "SI");
  CHECK(extra.min == 0 && extra.max == 100);
}

TEST(style_names_round_trip) {
  for (GaugeKind k :
       {GaugeKind::kRadial, GaugeKind::kCompass, GaugeKind::kAttitude}) {
    GaugeKind parsed = GaugeKind::kRadial;
    CHECK(parse_page_style(page_style_name(k), &parsed));
    CHECK(parsed == k);
  }
  GaugeKind kind = GaugeKind::kCompass;
  CHECK(!parse_page_style("Off", &kind));
  CHECK(!parse_page_style(nullptr, &kind));
  CHECK(kind == GaugeKind::kCompass);  // Unchanged on failure
}

TEST(style_change_resets_untouched_fields) {
  const PageFields current = coolant();
  PageFields next = current;
  next.kind = GaugeKind::kAttitude;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.sk_path == "navigation.attitude");
  CHECK(next.title.empty());  // The style's default
  CHECK(next.decimals == style_template(GaugeKind::kAttitude).decimals);

  next = current;
  next.kind = GaugeKind::kCompass;
  next.sk_path = "navigation.headingMagnetic";  // Typed in the same save
  next.title = "MAG";
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.sk_path == "navigation.headingMagnetic");
  CHECK(next.title == "MAG");

  // Compass -> Dial: a pathless SI dial on 0..100.
  PageFields compass = factory_fields(default_page(Side::kPort, 2));
  next = compass;
  next.kind = GaugeKind::kRadial;
  CHECK(finish_page_save(compass, &next, true) == PageCheck::kOk);
  CHECK(next.sk_path.empty());
  CHECK(next.unit_id == "SI");
  CHECK(next.min == 0 && next.max == 100 && !next.manual_scale);
}

TEST(boot_load_keeps_saved_fields) {
  // At boot `current` is the factory page; a saved compass on that card is
  // loaded as is, not reset.
  const PageFields factory = coolant();
  PageFields saved = factory;
  saved.kind = GaugeKind::kCompass;
  saved.sk_path = "navigation.headingTrue";
  saved.title = "Saved";
  PageFields next = saved;
  CHECK(finish_page_save(factory, &next, false) == PageCheck::kOk);
  CHECK(next.kind == GaugeKind::kCompass);
  CHECK(next.title == "Saved");
  CHECK(next.min == factory.min && next.max == factory.max);
}

TEST(empty_path_falls_back_by_style) {
  PageFields current = factory_fields(default_page(Side::kPort, 2));
  PageFields next = current;
  next.sk_path = "";
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.sk_path == "navigation.headingTrue");

  current = coolant();
  next = current;
  next.sk_path = "";  // A cleared dial path stays cleared
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.sk_path.empty());
}

TEST(unit_change_converts_the_range_only_on_the_same_path) {
  const PageFields current = coolant();
  PageFields next = current;
  next.unit_id = "\xC2\xB0" "F";
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.min == 0 && next.max == 250);

  next = current;  // Path changed in the same save: range kept
  next.unit_id = "\xC2\xB0" "F";
  next.sk_path = "propulsion.port.coolantTemperature";
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.min == 0 && next.max == 120);

  next = current;  // Range typed in the same save: kept
  next.unit_id = "\xC2\xB0" "F";
  next.min = 100;
  next.max = 260;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(next.min == 100 && next.max == 260);

  // A new page (no path yet) given a path and a unit: 0..100 stays, never
  // read as kelvin.
  const PageFields fresh = factory_fields(nullptr);
  next = fresh;
  next.sk_path = "environment.water.temperature";
  next.unit_id = "\xC2\xB0" "C";
  CHECK(finish_page_save(fresh, &next, true) == PageCheck::kOk);
  CHECK(next.min == 0 && next.max == 100);

  next = current;  // Not on the boot load
  next.unit_id = "\xC2\xB0" "F";
  CHECK(finish_page_save(current, &next, false) == PageCheck::kOk);
  CHECK(next.min == 0 && next.max == 120);
}

TEST(bad_values_are_refused) {
  const PageFields current = coolant();
  PageFields next = current;
  next.unit_id = "furlong";
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadUnit);
  next = current;
  next.min = 50;
  next.max = 50;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadRange);
  next = current;
  next.max = NAN;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadRange);
  next = current;
  next.decimals = 4;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadDecimals);
  next = current;
  next.decimals = -1;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadDecimals);
  next = current;
  next.timeout_s = 0;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadTimeout);
  next = current;
  next.timeout_s = 601;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kBadTimeout);
}

TEST(schema_is_valid_json_with_the_style_fields) {
  for (GaugeKind k :
       {GaugeKind::kRadial, GaugeKind::kCompass, GaugeKind::kAttitude}) {
    const std::string text = page_schema(k, 7);
    JsonDocument doc;
    CHECK(deserializeJson(doc, text) == DeserializationError::Ok);
    JsonObjectConst props = doc["properties"];
    CHECK(props["style"]["items"]["enum"].size() == 3);
    CHECK(props["sk_path"].is<JsonObjectConst>());
    CHECK(props["timeout_s"]["maximum"] == kMaxTimeoutS);
    CHECK(props["reset"]["type"] == "boolean");
    CHECK(props["swap_with"]["maximum"] == 7);
    const bool dial = k == GaugeKind::kRadial;
    CHECK(props["unit"].is<JsonObjectConst>() == dial);
    CHECK(props["min"].is<JsonObjectConst>() == dial);
    CHECK(props["show_zones"].is<JsonObjectConst>() == dial);
    if (dial) {
      // A select is an array schema; every display unit is offered.
      CHECK(props["unit"]["type"] == "array");
      CHECK(props["unit"]["items"]["enum"].size() > 10);
    }
  }
}

TEST(save_actions) {
  // The boot load is always a plain load; actions are never stored.
  CHECK(page_save_action(false, false, true, 3, 0, 6) == PageAction::kSave);
  // A web save from an out-of-date form is refused, whatever it asks.
  CHECK(page_save_action(true, false, false, 0, 0, 6) == PageAction::kStale);
  CHECK(page_save_action(true, false, true, 0, 0, 6) == PageAction::kStale);
  CHECK(page_save_action(true, false, false, 2, 0, 6) == PageAction::kStale);
  // Reset wins over a swap in the same save.
  CHECK(page_save_action(true, true, true, 2, 0, 6) == PageAction::kReset);
  CHECK(page_save_action(true, true, false, 2, 0, 6) == PageAction::kSwap);
  CHECK(page_save_action(true, true, false, 6, 0, 6) == PageAction::kSwap);
  CHECK(page_save_action(true, true, false, 7, 0, 6) == PageAction::kBadSwap);
  // With itself, 0 or below: a plain save.
  CHECK(page_save_action(true, true, false, 1, 0, 6) == PageAction::kSave);
  CHECK(page_save_action(true, true, false, 0, 0, 6) == PageAction::kSave);
  CHECK(page_save_action(true, true, false, -2, 0, 6) == PageAction::kSave);
}

TEST(only_adjusted_saves_change_the_form) {
  // same_fields tells a save the rules adjusted (the form must reload) from
  // a plain one (the card can be saved again).
  const PageFields current = coolant();
  PageFields next = current;
  next.title = "Engine water";
  PageFields submitted = next;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(same_fields(submitted, next));
  next = current;
  next.kind = GaugeKind::kCompass;  // Style change: path and title reset
  submitted = next;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(!same_fields(submitted, next));
  next = current;
  next.unit_id = "\xC2\xB0" "F";  // Range converted
  submitted = next;
  CHECK(finish_page_save(current, &next, true) == PageCheck::kOk);
  CHECK(!same_fields(submitted, next));
}
