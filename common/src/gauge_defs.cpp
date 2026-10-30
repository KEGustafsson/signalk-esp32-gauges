#include "gauge_defs.h"

namespace skgauges {

namespace {
constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kHzToRpm = 60.0f;
constexpr float kMsToKnots = 1.943844492f;  // 3600 / 1852
constexpr float kKelvinToCelsius = -273.15f;

constexpr uint32_t kOrange = 0xFF7A1A;
constexpr uint32_t kRed = 0xE01010;
}  // namespace

// Order = swipe order; a unit only shows its own side's engine pages, so a
// starboard unit swipes: STB Engine, Speed, Heading, STB Coolant, STB Oil,
// Sea horizon. The first page is the one a freshly flashed unit starts on.
const GaugeDef kGauges[] = {
    // id, kind, side, title, unit, sk_path, scale, offset, min, max,
    // major_step, minor_per_major, decimals, listen_delay_ms, zones, count
    {"port_rpm", GaugeKind::kRadial, Side::kPort, "PRT Engine", "rpm",
     "propulsion.port.revolutions", kHzToRpm, 0.0f, 0.0f, 3000.0f,
     500.0f, 5, 0, 250, {{2500.0f, 3000.0f, kOrange}}, 1},

    {"stbd_rpm", GaugeKind::kRadial, Side::kStarboard, "STB Engine", "rpm",
     "propulsion.starboard.revolutions", kHzToRpm, 0.0f, 0.0f, 3000.0f,
     500.0f, 5, 0, 250, {{2500.0f, 3000.0f, kOrange}}, 1},

    {"speed", GaugeKind::kRadial, Side::kBoth, "Speed", "kn",
     "navigation.speedOverGround", kMsToKnots, 0.0f, 0.0f, 30.0f,
     5.0f, 5, 1, 250, {}, 0},

    {"heading", GaugeKind::kCompass, Side::kBoth, "HEADING", "\xC2\xB0T",
     "navigation.headingTrue", kRadToDeg, 0.0f, 0.0f, 360.0f,
     30.0f, 6, 0, 200, {}, 0},

    {"port_coolant", GaugeKind::kRadial, Side::kPort, "Coolant",
     "\xC2\xB0" "C", "propulsion.port.temperature", 1.0f,
     kKelvinToCelsius, 0.0f, 120.0f, 20.0f, 4, 0, 1000,
     {{100.0f, 120.0f, kRed}}, 1},

    {"stbd_coolant", GaugeKind::kRadial, Side::kStarboard, "Coolant",
     "\xC2\xB0" "C", "propulsion.starboard.temperature", 1.0f,
     kKelvinToCelsius, 0.0f, 120.0f, 20.0f, 4, 0, 1000,
     {{100.0f, 120.0f, kRed}}, 1},

    {"port_oil", GaugeKind::kRadial, Side::kPort, "Oil", "\xC2\xB0" "C",
     "propulsion.port.oilTemperature", 1.0f, kKelvinToCelsius, 0.0f,
     120.0f, 20.0f, 4, 0, 1000, {{100.0f, 120.0f, kRed}}, 1},

    {"stbd_oil", GaugeKind::kRadial, Side::kStarboard, "Oil", "\xC2\xB0" "C",
     "propulsion.starboard.oilTemperature", 1.0f, kKelvinToCelsius, 0.0f,
     120.0f, 20.0f, 4, 0, 1000, {{100.0f, 120.0f, kRed}}, 1},

    // Heel (roll) on the arc and LCD, trim (pitch) on the small LCD.
    // Range is the heel scale: +-40 degrees; zones are mirrored to port.
    {"attitude", GaugeKind::kAttitude, Side::kBoth, "HEEL", "\xC2\xB0",
     "navigation.attitude", kRadToDeg, 0.0f, -40.0f, 40.0f,
     10.0f, 2, 1, 200,
     {{-10.0f, 10.0f, 0x2EB82E}, {10.0f, 20.0f, 0xE8C21A},
      {20.0f, 40.0f, kRed}},
     3},
};

const size_t kGaugeCount = sizeof(kGauges) / sizeof(kGauges[0]);
static_assert(sizeof(kGauges) / sizeof(kGauges[0]) <= kMaxGauges,
              "kGauges has more pages than kMaxGauges");

}  // namespace skgauges
