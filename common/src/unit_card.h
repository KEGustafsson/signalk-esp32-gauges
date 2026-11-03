#pragma once

// The unit's identity, all of it set in the web UI's "Unit" card
// (unit_identity.h has the rules and a fresh unit's values):
//  - Role: Starboard / Port / General (no engine);
//  - Hostname: SensESP's own setting (/system/hostname);
//  - Display name: shown on the gauge's info overlay and in the boot log.
// The role and display name are saved in UnitSettings (/Unit/settings),
// which is read at boot before SensESP starts; the hostname stays where
// SensESP keeps it. A change applies when the unit restarts, which the web
// UI then offers.

#include <string>

#include "gauge_defs.h"
#include "sensesp/system/saveable.h"

namespace skgauges {

// The role and display name saved on the unit. Safe to load before
// SensESP's app exists (a plain file read).
class UnitSettings : public sensesp::FileSystemSaveable {
 public:
  UnitSettings() : sensesp::FileSystemSaveable("/Unit/settings") {}

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;

  bool has_role = false;  // False: none saved yet (take it from the hostname)
  Side role = Side::kStarboard;
  std::string name;  // Empty: the role's default display name
};

UnitSettings& unit_settings();

class UnitCard : public sensesp::FileSystemSaveable {
 public:
  UnitCard(const char* board, const char* board_label, Side role);

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;
  bool load() override { return true; }  // UnitSettings holds the data
  bool save() override;

 private:
  const char* board_;
  const char* board_label_;
  Side role_;  // As the unit runs now, or as last saved
};

// Adds the card to the web UI; call once SensESP's app exists, with the
// role the unit started with.
void add_unit_card(const char* board, const char* board_label, Side role);

}  // namespace skgauges

namespace sensesp {
const String ConfigSchema(const skgauges::UnitCard&);
}  // namespace sensesp
