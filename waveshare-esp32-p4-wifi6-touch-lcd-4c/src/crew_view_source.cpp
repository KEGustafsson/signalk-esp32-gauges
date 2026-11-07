#include "crew_view_source.h"

#include <esp_timer.h>

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
    case RadioState::kNoNetwork: return ui::RadioState::kNoNetwork;
    case RadioState::kPaused: return ui::RadioState::kPausedMemory;
    case RadioState::kStopped: return ui::RadioState::kStopped;
  }
  return ui::RadioState::kOff;
}

// A running radio's supervisor (cr-beat) writes the status once a second. If
// it has not for this long, nothing on the screen can be confirmed: the
// radio is shown stopped rather than on channel with its last roster.
constexpr int64_t kBeatStaleMs = 5000;

bool running(crewradio::RadioState s) {
  using crewradio::RadioState;
  return s == RadioState::kNoClock || s == RadioState::kOnChannel ||
         s == RadioState::kPaused || s == RadioState::kNoNetwork;
}

}  // namespace

bool crew_view_from_radio(ui::CrewView* out) {
  crewradio::esp::RadioStatus s = crewradio::esp::status();
  if (running(s.state) &&
      esp_timer_get_time() / 1000 - s.beat_ms > kBeatStaleMs) {
    s.state = crewradio::RadioState::kStopped;
    s.members.clear();
    s.talking = false;
  }
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
    // Over Reticulum: heard on this unit's own Reticulum link, or through a
    // relay from a member that says it is on Reticulum.
    c.reticulum = m.reticulum ||
                  (c.relayed && (m.transports & crewradio::Hello::kReticulum));
    c.talking = m.talking;
    c.speaker = false;  // Nothing on the wire tells a person from the plugin
    out->members.push_back(c);
  }
  return true;
}

}  // namespace skgauges
