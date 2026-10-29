#pragma once

// WiFi/OTA credentials and gauge addresses live in the repository's top-level
// include/secrets.h, which is GITIGNORED and never committed -- create it from
// include/secrets.example.h. Both projects' platformio.ini put that directory
// on the include path. Without a secrets.h the firmware still builds from the
// example's placeholders; it just won't join WiFi until you create secrets.h
// or configure WiFi through the SensESP setup portal.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
