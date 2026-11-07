#pragma once

// The 4"'s Crew Radio as the gauges' screen sees it: the radio's status
// (p4-gauge-crew-radio) turned into the UI's CrewView (common/src/ui), the
// one place that knows both. Registered in main.cpp before the UI is built.

#include "ui/crew_view.h"

namespace skgauges {

bool crew_view_from_radio(ui::CrewView* out);
// The Crew page's - and + (ui::CrewAdjust): one step to the radio's card.
bool crew_adjust_radio(ui::CrewLevel level, int dir);

}  // namespace skgauges
