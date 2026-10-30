---
name: build-flash
description: >
  Build gauge firmware, flash it to a unit, and monitor serial output. Use
  when user wants to test, upload, flash, or "try it on the device".
argument-hint: "[optional: p4_stbd, p4_port, s3_stbd or s3_port]"
---

# Build, Flash, and Monitor

## Phase 0: Pick the unit

Ask which one if it was not given:

| Device | Project | OTA env | USB env |
|---|---|---|---|
| 4" starboard | `waveshare-esp32-p4-wifi6-touch-lcd-4c/` | `p4_stbd` | `p4_stbd_usb` |
| 4" port | `waveshare-esp32-p4-wifi6-touch-lcd-4c/` | `p4_port` | `p4_port_usb` |
| 2.1" starboard | `waveshare-esp32-s3-touch-lcd-2.1/` | `s3_stbd` | `s3_stbd_usb` |
| 2.1" port | `waveshare-esp32-s3-touch-lcd-2.1/` | `s3_port` | `s3_port_usb` |

The OTA env's address and password come from the gitignored
`include/secrets.h` (`SECRET_OTA_HOST_<ENV>`, `SECRET_OTA_PASSWORD`).

## Phase 1: Build

1. Run `pio run -e <env>` from the unit's project folder.
2. **If it succeeds**: report the binary size and move to Phase 2.
3. **If it fails**: translate the error into plain language, fix it, rebuild.

## Phase 2: Flash

- **First flash** (or a unit not on WiFi): the `_usb` env over USB-C,
  `pio run -e <env>_usb -t upload`.
- **Later**: `pio run -e <env> -t upload` goes over the air.

If an OTA upload fails:
- `ota_auth: … does not exist` -> no `include/secrets.h`; create it from
  `secrets.example.h`.
- `defines no non-empty SECRET_OTA_PASSWORD` -> set one; then flash once over
  USB so the unit knows it.
- "No response from device" -> the unit is off, off the network, or at another
  address; override once with `--upload-port <address>`.
- "Authentication Failed" -> the unit was flashed with a different OTA password.

## Phase 3: Monitor and Configure

1. `pio device monitor` from the project folder (2.1": CH343 UART; 4": USB
   Serial/JTAG on the USB-C port).
2. Interpret the output: display init, WiFi, Signal K connection, crashes
   (`esp32_exception_decoder` symbolises backtraces).
3. On the screen: the gauge appears with dashes; tap shows the IP and the
   Signal K state; the bezel dot is SensESP's status LED.
4. A unit without WiFi credentials in `secrets.h` raises SensESP's access
   point named after its hostname (password `SECRET_AP_PASSWORD`, or
   SensESP's default when that is empty); configure WiFi and the server in
   its web UI. With the boat's WiFi in `secrets.h` and no AP password, the
   access point stays off.
5. In the Signal K admin UI, approve the unit's access request.

## Phase 4: Evaluate

- **Needles move**: compare against another instrument -- a moving needle is
  not proof of the right path or unit conversion.
- **Dashes stay**: wrong SK path (fix in the web UI), no data on the server, or
  the access request not approved.
- **Touch dead**: the serial log says "Touch did not start" after the
  firmware tried both GT911 addresses (0x5D/0x14, `common/src/board_start.cpp`)
  -- check the touch cable and the board README's first power-up checklist.
- **Dark 4" screen**: backlight polarity (`ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL`).
- **Reboot loop**: read the backtrace; display init failure and LVGL asserts
  restart the unit by design.
