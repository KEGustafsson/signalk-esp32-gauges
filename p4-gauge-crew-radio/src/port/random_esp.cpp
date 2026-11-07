// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "random.h"

#include <esp_random.h>

namespace crewradio {
namespace port {

void random_bytes(uint8_t* out, size_t len) { esp_fill_random(out, len); }

}  // namespace port
}  // namespace crewradio
#endif
