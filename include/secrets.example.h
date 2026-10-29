#pragma once

// TEMPLATE -- copy this file to `secrets.h` (same directory) and fill in your
// real values.
//
// `secrets.h` is gitignored and never committed. It holds everything specific
// to this boat: the credentials, which are compiled straight into the firmware
// (a binary built from a real one carries the WiFi password and the OTA
// password), and the addresses of the four gauges.
//
// One file serves both firmware projects (waveshare-esp32-p4-wifi6-touch-lcd-4c
// and waveshare-esp32-s3-touch-lcd-2.1): their platformio.ini puts this
// directory on the include path, and scripts/ota_auth.py reads it at upload
// time. common/src/gauge_secrets.h includes secrets.h when it exists and
// falls back to this example otherwise, so a fresh clone still builds -- it
// just will not join WiFi until you create secrets.h or configure WiFi
// through the SensESP setup portal (access point named after the unit's
// hostname).
//
// These values only seed a unit that has no saved settings: once WiFi or the
// Signal K server has been saved on the unit (web UI, or SensESP saving its
// access token), the saved values win over a later edit here.

// Set a strong password here to enable OTA updates. It stays empty in this
// committed example, so a build without a secrets.h never accepts OTA
// uploads (scripts/ota_auth.py also refuses to upload without one). The
// upload passes it on a shell command line, so use only letters, digits and
// . _ ~ + = @ : , / - (scripts/ota_auth.py refuses anything else).
#define SECRET_OTA_PASSWORD ""

// The SensESP web UI. With a password set it asks for this login; empty
// leaves it open to anyone who can reach the unit (and it can repoint the
// gauge at another Signal K server).
#define SECRET_ADMIN_USER "admin"
#define SECRET_ADMIN_PASSWORD ""

// SensESP's setup access point (SSID = the unit's hostname). Seeds a unit
// with no saved WiFi settings only; see README.md to re-apply it later.
//  - set (8+ characters): the access point uses this password;
//  - empty, with a WiFi SSID and password set: the access point is off;
//  - empty, no WiFi configured: SensESP's default ("thisisfine"), so a fresh
//    unit can still be set up from its web UI at 192.168.4.1.
#define SECRET_AP_PASSWORD ""

// The address of each gauge. scripts/ota_auth.py uploads to it; the suffix is
// the PlatformIO environment name in upper case: env:p4_stbd -> ..._P4_STBD.
// An IP or an mDNS name both work for uploads. With SECRET_USE_STATIC_IP
// below, each gauge also configures its own entry as its static WiFi address.
#define SECRET_OTA_HOST_P4_STBD "192.168.220.130"  // 4" #1, starboard
#define SECRET_OTA_HOST_P4_PORT "192.168.220.131"  // 4" #2, port
#define SECRET_OTA_HOST_S3_STBD "192.168.220.132"  // 2.1" #1, starboard
#define SECRET_OTA_HOST_S3_PORT "192.168.220.133"  // 2.1" #2, port

// 0: the gauges get their address by DHCP (reserve the addresses above in the
//    router so OTA keeps finding them).
// 1: each gauge sets its SECRET_OTA_HOST_* address as a static IP on every
//    WiFi network below (all of them -- so only use this when they share the
//    boat's subnet), with this netmask, gateway and DNS server.
#define SECRET_USE_STATIC_IP 0
#define SECRET_NETMASK "255.255.255.0"
#define SECRET_GATEWAY "192.168.220.1"
#define SECRET_DNS "192.168.220.1"

// The boat's signalk-server. Compiled into all four gauges as the default,
// and still overridable per device through SensESP's web config UI. An IP or
// an mDNS name both work. Leave the address empty to let SensESP find the
// server by mDNS.
#define SECRET_SK_SERVER_ADDRESS "signalk.local"
#define SECRET_SK_SERVER_PORT 3000

// The boat's WiFi. Up to three networks; leave the unused pairs empty.
// Empty here, so a build without secrets.h comes up with the setup portal.
#define SECRET_WIFI_SSID ""
#define SECRET_WIFI_PASSWORD ""
#define SECRET_WIFI_SSID_2 ""
#define SECRET_WIFI_PASSWORD_2 ""
#define SECRET_WIFI_SSID_3 ""
#define SECRET_WIFI_PASSWORD_3 ""
