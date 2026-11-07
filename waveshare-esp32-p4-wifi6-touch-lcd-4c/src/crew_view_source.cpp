#include "crew_view_source.h"

#include "core/hello.h"
#include "text_fold.h"
#include "esp/radio_runtime.h"

namespace skgauges {

namespace {

ui::RadioState ui_state(crewradio::RadioState s) {
  using crewradio::RadioState;
  switch (s) {
    case RadioState::kOff: return ui::RadioState::kOff;
    case RadioState::kNoKey: return ui::RadioState::kNoKey;
    case RadioState::kSelfTestFailed: return ui::RadioState::kSelfTestFailed;
    case RadioState::kNoAudio: return ui::RadioState::kNoAudio;
    case RadioState::kNoClock: return ui::RadioState::kNoClock;
    case RadioState::kOnChannel: return ui::RadioState::kOnChannel;
    case RadioState::kPaused: return ui::RadioState::kPausedMemory;
    case RadioState::kStopped: return ui::RadioState::kStopped;
  }
  return ui::RadioState::kOff;
}

}  // namespace

bool crew_view_from_radio(ui::CrewView* out) {
  const crewradio::esp::RadioStatus s = crewradio::esp::status();
  // Names in the fonts' ASCII (text_fold.h): a Mäkelä is drawn as Makela,
  // not as Mkel.
  out->channel = s.channel.empty() ? "CREW" : ascii_fold(s.channel);
  out->state = ui_state(s.state);
  out->muted = false;  // The radio has no mute of its own yet
  out->transmitting = s.talking;
  out->volume = (uint8_t)(s.volume < 0 ? 0 : s.volume > 100 ? 100 : s.volume);
  out->volume_max = 100;
  out->members.clear();
  for (const crewradio::Member& m : s.members) {
    ui::CrewMember c;
    c.name = m.name.empty() ? "?" : ascii_fold(m.name);  // "?": no hello yet
    c.bars = (uint8_t)m.bars;
    c.relayed = m.hops > 0;
    // A member is known to be over Reticulum only when it says so and
    // reached us through a relay (this unit has no Reticulum link itself).
    c.reticulum = c.relayed && (m.transports & crewradio::Hello::kReticulum);
    c.talking = m.talking;
    c.speaker = false;  // Nothing on the wire tells a person from the plugin
    out->members.push_back(c);
  }
  return true;
}

}  // namespace skgauges
