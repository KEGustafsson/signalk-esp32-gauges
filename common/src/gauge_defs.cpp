#include "gauge_defs.h"

namespace skgauges {

namespace {

constexpr uint32_t kRed = 0xE01010;

// id, kind, title, unit_id, sk_path, min, max, major_step, minor_per_major,
// decimals, listen_delay_ms, fixed bands, count
const GaugeDef kPortRpm = {
    "port_rpm", GaugeKind::kRadial, "PRT Engine", "rpm",
    "propulsion.port.revolutions", 0.0f, 3000.0f, 500.0f, 5, 0, 250, {}, 0};
const GaugeDef kStbdRpm = {
    "stbd_rpm", GaugeKind::kRadial, "STB Engine", "rpm",
    "propulsion.starboard.revolutions", 0.0f, 3000.0f, 500.0f, 5, 0, 250, {},
    0};
const GaugeDef kSpeed = {
    "speed", GaugeKind::kRadial, "Speed", "kn", "navigation.speedOverGround",
    0.0f, 30.0f, 5.0f, 5, 1, 250, {}, 0};
const GaugeDef kHeading = {
    "heading", GaugeKind::kCompass, "HEADING", "\xC2\xB0",
    "navigation.headingTrue", 0.0f, 360.0f, 30.0f, 6, 0, 200, {}, 0};
const GaugeDef kPortCoolant = {
    "port_coolant", GaugeKind::kRadial, "Coolant", "\xC2\xB0" "C",
    "propulsion.port.temperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000, {}, 0};
const GaugeDef kStbdCoolant = {
    "stbd_coolant", GaugeKind::kRadial, "Coolant", "\xC2\xB0" "C",
    "propulsion.starboard.temperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000, {},
    0};
const GaugeDef kPortOil = {
    "port_oil", GaugeKind::kRadial, "Oil", "\xC2\xB0" "C",
    "propulsion.port.oilTemperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000, {}, 0};
const GaugeDef kStbdOil = {
    "stbd_oil", GaugeKind::kRadial, "Oil", "\xC2\xB0" "C",
    "propulsion.starboard.oilTemperature", 0.0f, 120.0f, 20.0f, 4, 0, 1000,
    {}, 0};
// Heel (roll) on the arc and LCD, trim (pitch) on the small LCD. Range is
// the heel scale: +-40 degrees; the green/yellow/red bands are part of the
// inclinometer scale (mirrored to port), not alarm zones.
const GaugeDef kAttitude = {
    "attitude", GaugeKind::kAttitude, "HEEL", "\xC2\xB0", "navigation.attitude",
    -40.0f, 40.0f, 10.0f, 2, 1, 200,
    {{-10.0f, 10.0f, 0x2EB82E}, {10.0f, 20.0f, 0xE8C21A}, {20.0f, 40.0f, kRed}},
    3};
// A dial switched on in the web UI: the path's value as sent (SI), on
// 0..100 until a scale is set or the server sends meta.displayScale.
const GaugeDef kDial = {
    "dial", GaugeKind::kRadial, "Gauge", "SI", "", 0.0f, 100.0f, 20.0f, 4, 1,
    500, {}, 0};

// Factory pages in swipe order; a device shows its own side's engine. The
// first page is the one a freshly flashed device starts on.
const GaugeDef* const kPortPages[] = {&kPortRpm,     &kSpeed,   &kHeading,
                                      &kPortCoolant, &kPortOil, &kAttitude};
const GaugeDef* const kStbdPages[] = {&kStbdRpm,     &kSpeed,   &kHeading,
                                      &kStbdCoolant, &kStbdOil, &kAttitude};
constexpr size_t kSidePageCount = sizeof(kPortPages) / sizeof(kPortPages[0]);
static_assert(sizeof(kStbdPages) == sizeof(kPortPages),
              "both sides have the same number of factory pages");
static_assert(kSidePageCount <= kMaxPages, "more factory pages than slots");

}  // namespace

const GaugeDef* const kFactoryPages[] = {
    &kPortRpm, &kStbdRpm,     &kSpeed,   &kHeading, &kPortCoolant,
    &kStbdCoolant, &kPortOil, &kStbdOil, &kAttitude};
const size_t kFactoryPageCount =
    sizeof(kFactoryPages) / sizeof(kFactoryPages[0]);

const GaugeDef* default_page(Side side, size_t slot) {
  if (slot >= kSidePageCount) {
    return nullptr;
  }
  return side == Side::kPort ? kPortPages[slot] : kStbdPages[slot];
}

const GaugeDef& style_template(GaugeKind kind) {
  switch (kind) {
    case GaugeKind::kCompass:
      return kHeading;
    case GaugeKind::kAttitude:
      return kAttitude;
    case GaugeKind::kRadial:
    default:
      return kDial;
  }
}

}  // namespace skgauges
