#include "gauge_version.h"

// Generated from VERSION at build time; the only file that includes it, so
// a new version recompiles only this one.
#if __has_include("gauge_version_value.h")
#include "gauge_version_value.h"
#endif

#ifndef GAUGE_FIRMWARE_VERSION
#define GAUGE_FIRMWARE_VERSION "unknown"
#endif

namespace skgauges {

const char* firmware_version() { return GAUGE_FIRMWARE_VERSION; }

}  // namespace skgauges
