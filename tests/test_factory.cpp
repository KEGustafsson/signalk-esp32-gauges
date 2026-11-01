// The factory pages and style templates (gauge_defs.h).

#include <string.h>

#include "gauge_defs.h"
#include "gauge_resolve.h"
#include "test.h"

using namespace skgauges;

TEST(factory_pages_follow_the_side) {
  CHECK(factory_page_count() == 6);
  CHECK(factory_page_count() <= kMaxPages);
  for (size_t slot = 0; slot < factory_page_count(); slot++) {
    const GaugeDef* port = default_page(Side::kPort, slot);
    const GaugeDef* stbd = default_page(Side::kStarboard, slot);
    CHECK(port != nullptr && stbd != nullptr);
    CHECK(port->kind == stbd->kind);
    // An engine page names its own side's engine, never the other one.
    CHECK(strstr(port->sk_path, "starboard") == nullptr);
    CHECK(strstr(stbd->sk_path, ".port.") == nullptr);
  }
  CHECK(strcmp(default_page(Side::kPort, 0)->sk_path,
               "propulsion.port.revolutions") == 0);
  CHECK(strcmp(default_page(Side::kStarboard, 0)->sk_path,
               "propulsion.starboard.revolutions") == 0);
  CHECK(default_page(Side::kPort, factory_page_count()) == nullptr);
  CHECK(default_page(Side::kStarboard, kMaxPages - 1) == nullptr);
}

TEST(style_templates) {
  CHECK(style_template(GaugeKind::kRadial).kind == GaugeKind::kRadial);
  CHECK(strcmp(style_template(GaugeKind::kRadial).sk_path, "") == 0);
  CHECK(strcmp(style_template(GaugeKind::kRadial).unit_id, "SI") == 0);
  CHECK(strcmp(style_template(GaugeKind::kCompass).sk_path,
               "navigation.headingTrue") == 0);
  CHECK(strcmp(style_template(GaugeKind::kAttitude).sk_path,
               "navigation.attitude") == 0);
}

TEST(factory_ticks_match_the_computed_ones) {
  // A page's ticks are computed from its range (gauge_settings.cpp); for
  // every factory dial they are the hand-tuned table values.
  for (size_t i = 0; i < kFactoryPageCount; i++) {
    const GaugeDef& def = *kFactoryPages[i];
    if (def.kind != GaugeKind::kRadial) continue;
    uint8_t minor = 0;
    const float step = nice_step(def.min, def.max, &minor);
    CHECK_NEAR(step, def.major_step, 1e-4);
    CHECK(minor == def.minor_per_major);
  }
}
