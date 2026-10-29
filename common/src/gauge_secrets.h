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

// Settings added after the first template: an older secrets.h still builds.
#ifndef SECRET_ADMIN_USER
#define SECRET_ADMIN_USER "admin"
#endif
#ifndef SECRET_ADMIN_PASSWORD
#define SECRET_ADMIN_PASSWORD ""
#endif
#ifndef SECRET_AP_PASSWORD
#define SECRET_AP_PASSWORD ""
#endif

// WPA2 needs 8+ characters; a shorter AP password silently gives no AP.
static_assert(sizeof(SECRET_AP_PASSWORD) == 1 || sizeof(SECRET_AP_PASSWORD) > 8,
              "SECRET_AP_PASSWORD must be empty or at least 8 characters");
// SensESP ignores the login when the user name is empty.
static_assert(sizeof(SECRET_ADMIN_PASSWORD) == 1 ||
                  sizeof(SECRET_ADMIN_USER) > 1,
              "SECRET_ADMIN_USER must be set when SECRET_ADMIN_PASSWORD is");
