#pragma once

// The web UI's "Unit" card: a Starboard / Port dropdown. The side itself is
// the unit's hostname (unit_identity.h); choosing one here sets that
// hostname (gauge-<board>-stbd / -port), and the unit takes the side at its
// next restart, which the web UI then offers.

#include "sensesp/system/saveable.h"

namespace skgauges {

class UnitCard : public sensesp::FileSystemSaveable {
 public:
  explicit UnitCard(const char* board);

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;
  // Nothing of its own to store: the hostname is SensESP's setting.
  bool load() override { return true; }
  bool save() override { return true; }

 private:
  const char* board_;
};

// Adds the card to the web UI; call once SensESP's app exists.
void add_unit_card(const char* board);

}  // namespace skgauges

namespace sensesp {
const String ConfigSchema(const skgauges::UnitCard&);
}  // namespace sensesp
