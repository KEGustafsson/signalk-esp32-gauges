// SPDX-License-Identifier: EUPL-1.2
#include "encode_budget.h"

namespace crewradio {

bool EncodeBudget::took(int us) {
  if (us > slowest_) slowest_ = us;
  if (++frames_ < kWindow) return false;
  const bool over = slowest_ > kHighUs;
  frames_ = 0;
  slowest_ = 0;
  if (!over || complexity_ == 0) return false;
  complexity_--;
  return true;
}

}  // namespace crewradio
