#include "gauge_defs.h"

namespace skgauges {

namespace {
constexpr uint32_t kRed = 0xE01010;
}  // namespace

// Order = swipe order; a unit only shows its own side's engine pages, so a
// starboard unit swipes: STB Engine, Speed, Heading, STB Coolant, STB Oil,
// Sea horizon (then any spare gauge switched on in the web UI). The first
// page is the one a freshly flashed unit starts on.
const GaugeDef kGauges[] = {
    // id, kind, side, title, unit_id, sk_path, min, max, major_step,
    // minor_per_major, decimals, listen_delay_ms, shown_by_default,
    // fixed bands, count
    {"port_rpm", GaugeKind::kRadial, Side::kPort, "PRT Engine", "rpm",
     "propulsion.port.revolutions", 0.0f, 3000.0f, 500.0f, 5, 0, 250, true,
     {}, 0},

    {"stbd_rpm", GaugeKind::kRadial, Side::kStarboard, "STB Engine", "rpm",
     "propulsion.starboard.revolutions", 0.0f, 3000.0f, 500.0f, 5, 0, 250,
     true, {}, 0},

    {"speed", GaugeKind::kRadial, Side::kBoth, "Speed", "kn",
     "navigation.speedOverGround", 0.0f, 30.0f, 5.0f, 5, 1, 250, true, {}, 0},

    {"heading", GaugeKind::kCompass, Side::kBoth, "HEADING", "\xC2\xB0",
     "navigation.headingTrue", 0.0f, 360.0f, 30.0f, 6, 0, 200, true, {}, 0},

    {"port_coolant", GaugeKind::kRadial, Side::kPort, "Coolant", "\xC2\xB0" "C",
     "propulsion.port.temperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000, true,
     {}, 0},

    {"stbd_coolant", GaugeKind::kRadial, Side::kStarboard, "Coolant", "\xC2\xB0" "C",
     "propulsion.starboard.temperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000,
     true, {}, 0},

    {"port_oil", GaugeKind::kRadial, Side::kPort, "Oil", "\xC2\xB0" "C",
     "propulsion.port.oilTemperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000, true,
     {}, 0},

    {"stbd_oil", GaugeKind::kRadial, Side::kStarboard, "Oil", "\xC2\xB0" "C",
     "propulsion.starboard.oilTemperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000,
     true, {}, 0},

    // Heel (roll) on the arc and LCD, trim (pitch) on the small LCD.
    // Range is the heel scale: +-40 degrees; the green/yellow/red bands are
    // part of the inclinometer scale (mirrored to port), not alarm zones.
    {"attitude", GaugeKind::kAttitude, Side::kBoth, "HEEL", "\xC2\xB0",
     "navigation.attitude", -40.0f, 40.0f, 10.0f, 2, 1, 200, true,
     {{-10.0f, 10.0f, 0x2EB82E}, {10.0f, 20.0f, 0xE8C21A},
      {20.0f, 40.0f, kRed}},
     3},

    // Spare gauges, hidden until switched on in the web UI: point them at
    // any numeric Signal K path and pick its unit, like a SKip gauge.
    {"gauge1", GaugeKind::kRadial, Side::kBoth, "Depth", "m",
     "environment.depth.belowTransducer", 0.0f, 50.0f, 10.0f, 5, 1, 500,
     false, {}, 0},

    {"gauge2", GaugeKind::kRadial, Side::kBoth, "Battery", "V",
     "electrical.batteries.house.voltage", 10.0f, 16.0f, 1.0f, 5, 1, 1000,
     false, {}, 0},
};

const size_t kGaugeCount = sizeof(kGauges) / sizeof(kGauges[0]);
static_assert(sizeof(kGauges) / sizeof(kGauges[0]) <= kMaxGauges,
              "kGauges has more pages than kMaxGauges");

}  // namespace skgauges
