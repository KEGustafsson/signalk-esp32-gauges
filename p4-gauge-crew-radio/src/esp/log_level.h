// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The radio's own log lines at INFO and WARN. Arduino-ESP32's prebuilt
// ESP-IDF has CONFIG_LOG_MAXIMUM_LEVEL 1 (errors only), and ESP-IDF's
// ESP_LOGx compile out anything above LOG_LOCAL_LEVEL, which defaults to
// it: a file that does not include Arduino.h (whose esp32-hal-log.h
// redefines the macros at CORE_DEBUG_LEVEL) printed only its errors, so
// "ES7210 at 0x40", "Talking" and every warning of these files were never
// seen. The runtime level (SensESP's SetupLogging) still filters them.
//
// Include this first, before any ESP-IDF header, in every src/esp and
// src/port file that logs. Per file, not a -D flag: a global flag would
// recompile the whole firmware (AGENTS.md).

#ifdef LOG_LOCAL_LEVEL
#undef LOG_LOCAL_LEVEL
#endif
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
