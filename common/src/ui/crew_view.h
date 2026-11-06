#pragma once

// What the Crew Radio shows on a gauge: the snapshot the radio would publish
// for the UI (p4-gauge-crew-radio/README.md: `cr-beat` swaps it in, the UI
// reads it without locks) and the drawing code reads. No LVGL, no radio code.
//
// MOCK-UP: nothing in the firmware fills this yet. The preview
// (tools/preview) renders the halo and the Crew page from sample views so
// the design can be looked at before any radio code exists.

#include <stdint.h>

#include <string>
#include <vector>

namespace skgauges {
namespace ui {

enum class RadioState : uint8_t {
  kOff,             // Radio disabled on this unit: nothing is drawn
  kStarting,        // The packet key is being derived
  kOnChannel,
  kNoClock,         // No wall clock: every packet would be stale
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
  bool anyone_talking() const {
    for (const CrewMember& m : members) {
      if (m.talking) return true;
    }
    return false;
  }
};

}  // namespace ui
}  // namespace skgauges
