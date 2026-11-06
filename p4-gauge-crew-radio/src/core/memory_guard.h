// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The shedding ladder for the C6 link's memory. On the 4" the ESP-Hosted
// link allocates its SDIO buffers from internal DMA-capable RAM while data
// flows, and when that runs out the link stalls for good (4" README). The
// radio is traffic on that link, so it steps down before it gets there: at
// each floor one more thing is shed, worst last, and it climbs back one step
// at a time only with kHysteresis to spare, so it does not flap. One step
// per update(), called once a second.
//
// The floors are provisional: 3.3.12 leaves about 289 KB free a minute after
// boot, and the stall was seen at 9 KB. Phase 1's bring-up measures them
// against the `heap` line with two talkers.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {

enum class Shed : uint8_t {
  kNone,         // Everything on
  kReticulum,    // Reticulum off: the boat before a link ashore
  kGroupCopies,  // Audio unicast only (hellos keep their group copies)
  kTransmit,     // Nothing sent, hellos included
  kPaused,       // Nothing received either: "radio paused: memory"
};

class MemoryGuard {
 public:
  // Free internal DMA RAM below floors[i] means at least Shed(i + 1).
  static constexpr size_t kFloors[4] = {96 * 1024, 64 * 1024, 48 * 1024,
                                        32 * 1024};
  static constexpr size_t kHysteresis = 16 * 1024;

  Shed update(size_t free_dma_bytes) {
    const int level = (int)level_;
    if (level < 4 && free_dma_bytes < kFloors[level]) {
      level_ = (Shed)(level + 1);
    } else if (level > 0 && free_dma_bytes >= kFloors[level - 1] + kHysteresis) {
      level_ = (Shed)(level - 1);
    }
    return level_;
  }
  Shed level() const { return level_; }

 private:
  Shed level_ = Shed::kNone;
};

}  // namespace crewradio
