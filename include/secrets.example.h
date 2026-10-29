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
// time. Each main.cpp includes secrets.h when it exists and falls back to this
// example otherwise, so a fresh clone still builds -- it just will not join
// WiFi until you create secrets.h or configure WiFi through the SensESP setup
// portal (access point named after the unit's hostname).

#define SECRET_OTA_PASSWORD "set-a-strong-ota-password"

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
//    WiFi network below, with this netmask, gateway and DNS server.
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
#define SECRET_WIFI_SSID "your-wifi-ssid"
#define SECRET_WIFI_PASSWORD "your-wifi-password"
#define SECRET_WIFI_SSID_2 ""
#define SECRET_WIFI_PASSWORD_2 ""
#define SECRET_WIFI_SSID_3 ""
#define SECRET_WIFI_PASSWORD_3 ""
