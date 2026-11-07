#pragma once

// What the Crew Radio shows on a gauge: a copy of the radio's status, taken
// under its lock (briefly) by the view source on the 4" and read by the
// drawing code without one. No LVGL, no radio code.
//
// On the 4" the radio fills it (the firmware registers a CrewViewSource);
// elsewhere, and in the preview without one, read_crew_view() says the
// radio is off. The preview renders the halo and the Crew page from sample
// views.

#include <stdint.h>

#include <string>
#include <vector>

namespace skgauges {
namespace ui {

enum class RadioState : uint8_t {
  kOff,             // Radio disabled on this unit: nothing is drawn
  kStarting,        // The packet key is being derived
  kNoKey,           // On, but no channel key saved
  kNoAudio,         // The codec or I2S did not start
  kStopped,         // Its tasks are not running (the gauge carries on)
  kOnChannel,
  kNoClock,         // No wall clock: every packet would be stale
  kNoNetwork,       // Neither the WLAN nor Reticulum up
  kPausedMemory,    // The DMA RAM guard has shed the radio
  kSelfTestFailed,  // The power-on known-answer test failed
};

struct CrewMember {
  std::string name;
  uint8_t bars = 4;      // 1..4, the app's LinkQuality rule
  bool relayed = false;  // Heard through a relay (hops > 0)
  bool reticulum = false;  // Heard over a Reticulum link (ashore, another boat)
  bool talking = false;
  bool speaker = false;  // A node without a person: the Signal K plugin

  bool operator==(const CrewMember& o) const {
    return name == o.name && bars == o.bars && relayed == o.relayed &&
           reticulum == o.reticulum && talking == o.talking &&
           speaker == o.speaker;
  }
  bool operator!=(const CrewMember& o) const { return !(*this == o); }
};

struct CrewView {
  std::string channel;  // The channel (boat) name set on the unit
  RadioState state = RadioState::kOff;
  bool muted = false;
  bool transmitting = false;
  uint8_t volume = 0;
  uint8_t volume_max = 0;
  // Everyone else on the channel, sorted by name so nobody moves around the
  // ring when someone joins. The head count is its size, as on the phones.
  std::vector<CrewMember> members;

  // The weakest link's bars; 0 with nobody else on the channel.
  uint8_t weakest_bars() const {
    uint8_t weakest = 0;
    for (const CrewMember& m : members) {
      if (weakest == 0 || m.bars < weakest) weakest = m.bars;
    }
    return weakest;
  }
  bool operator==(const CrewView& o) const {
    return channel == o.channel && state == o.state && muted == o.muted &&
           transmitting == o.transmitting && volume == o.volume &&
           volume_max == o.volume_max && members == o.members;
  }
  bool operator!=(const CrewView& o) const { return !(*this == o); }

  bool anyone_talking() const {
    for (const CrewMember& m : members) {
      if (m.talking) return true;
    }
    return false;
  }
};

// Where the view comes from: the firmware's radio, registered once at boot
// (set before the UI is built, so the halo is made). Called from the loop
// task; fills *out and returns true, or false for no radio.
using CrewViewSource = bool (*)(CrewView* out);
void set_crew_view_source(CrewViewSource source);
bool crew_view_available();
// The view now: the source's, or a radio that is off.
CrewView read_crew_view();

}  // namespace ui
}  // namespace skgauges
