#pragma once

// The firmware's version, from the repository's VERSION file
// (scripts/gauge_version.py). Shown in the boot log, on the info overlay
// and on the web UI's Status page.

namespace skgauges {

// E.g. "0.1.0"; "unknown" in a build that did not run the script.
const char* firmware_version();

}  // namespace skgauges
