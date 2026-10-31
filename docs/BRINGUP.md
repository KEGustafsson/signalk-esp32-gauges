# First power-up of a device

A step-by-step for flashing a device for the first time and checking it
works, with the serial log lines to expect at each step. The board-specific
details (touch address, backlight polarity, the 4"'s WiFi co-processor) are
in each board folder's README under *First power-up checklist*.

## 1. Before you start

- A `include/secrets.h` (copy `include/secrets.example.h`) with the boat's
  WiFi, the Signal K server, an OTA password and a web UI admin password.
- The device on USB-C. The 4" has two USB-C ports; either works for
  flashing, but its log may come out on the other one (board README).
- A Signal K server on the same network, with the paths the factory pages
  read (`propulsion.<side>.revolutions`, `navigation.speedOverGround`,
  `navigation.headingTrue`, ...), or at least one of them.

## 2. Flash over USB and open the log

From the board's folder, with the device's `_usb` env (here a 4" starboard
one; the 2.1" is `waveshare-esp32-s3-touch-lcd-2.1` and `s3_stbd_usb`):

```bash
cd waveshare-esp32-p4-wifi6-touch-lcd-4c
```

**4" only, before the first flash:** check the ESP32-P4's silicon revision.
The default build is for revision 3.0 or later (`board = esp32-p4_r3`), and
an older chip can refuse it before any log appears:

```bash
pio pkg exec -p tool-esptoolpy -- esptool --chip esp32p4 chip-id
```

It prints `Chip type: ESP32-P4 (revision v3.x)`. Below v3.0, set
`board = esp32-p4` in `platformio.ini` before flashing.

```bash
pio run -e p4_stbd_usb -t erase
```

```bash
pio run -e p4_stbd_usb -t upload
```

```bash
pio device monitor -e p4_stbd_usb
```

Once the monitor is connected, press the board's **RESET** button so the
log starts from power-on (opening the monitor does not always restart the
device, and the first lines would be missed). Erasing first is only needed
the first time (or to start clean): it clears every saved setting, the
Signal K access token included.

## 3. What the log should say

In this order, within a few seconds (times in ms since start):

| Line | Means | If it does not appear |
|---|---|---|
| `board: Display and touch started (touch at 0x..)` | The panel and the touch controller answered, at that I2C address (the 4"'s GT911: 0x5D, or 0x14 on some boards) | `Display board start failed (attempt n), restarting` repeating: a display fault. `Display started, without touch`: the touch controller did not answer (board README) |
| `boot: 4in STBD (gauge-4c-stbd): ESP32-P4 rev 3.02, 400 MHz, flash 32 MB, PSRAM 32 MB (... KB free), built ...` | Chip, silicon revision, memory | On the 4": see the revision check in step 2 |
| `boot: Display ready, building the gauge at ... ms` | | |
| `boot: 6 pages; filesystem ... of ... KB used` | The page cards were loaded | `Filesystem not mounted`: the partition table did not take; flash over USB again |
| `gauge_ui: Page page1 built in ... ms` | The first page is drawn | |
| `boot: Gauge on screen at ... ms` | | |
| `boot: SensESP set up at ... ms` | WiFi and Signal K are starting | |
| `boot: Setup done at ... ms; touch answers from here` | Touch works from here | |

Then, as the network comes up: SensESP's WiFi and WebSocket lines, and for
each page `gauges: pageN: metadata applied (...)` once the server sends the
path's metadata.

**4" only: the WiFi co-processor.** Right after `Gauge on screen` the log
reports the ESP32-C6's ESP-Hosted firmware. `Slave firmware version: 0.0.0`
(or `Version on Host is NEWER than version on co-processor`) means the C6
runs an older firmware than the P4 expects: WiFi connects, but the C6 does
not answer some of the P4's requests. Update the C6 once with
`tools/c6-update` (its README), then flash the gauge again. Afterwards the
log says `Versions Match!`.

Expected, not a fault:

- `ARDUINO: Invalid pin: 255`, `Pin 255 is already attached ...` and
  `No free timers available for freq=2000 ...`: SensESP's status LED class
  tries a GPIO at start-up; the gauge draws the status dot on the screen
  instead and never uses that pin.
- `W (...) ARDUINO: SPIFFS Already Mounted!`: the gauge mounts the
  filesystem before SensESP does.
- `Reason: 201 - NO_AP_FOUND` for one network of several: SensESP tries the
  networks in `secrets.h` in turn (the 4"'s C6 is 2.4 GHz only).

Warnings worth knowing:

- `loop: SensESP held the loop for ... ms` or `LVGL held ...`: something
  stalled the loop for 150 ms or more (touch feels slow). An occasional one
  during WiFi connect is normal; a steady stream is not.
- `gauges: pageN: the server gives ... in ..., the gauge converts from ...`:
  the page's display unit does not match the path's unit; its zones and
  scale are not used (`docs/PAGES.md`).
- `Page N not saved: ...`: a web UI save was refused, with the reason (an
  out-of-date form: reload the web UI page; `docs/PAGES.md`). `it could not
  be written`: the filesystem is full or failing; the page keeps its last
  saved setting.
- `An interrupted page save was finished or undone`: the power went off
  during a save; every page is as it was before that save, or after it.
  `... could not be undone`: the storage is failing; the pages show their
  factory settings and saves are refused until a restart recovers it.

## 4. On the screen

1. The first page appears with **dashes** in the LCD: nothing from Signal K
   yet. This is right.
2. Swipe left and right: every page comes up; a short toast names it.
3. Tap: the info box shows the device, its IP address, the Signal K
   connection state and the page's path. Tap again to close.
4. Long press: the backlight steps 100 → 60 → 30 → 10 %.
5. The small dot at the bottom of the bezel is the status: it turns blue,
   with a brief blink each second, once connected to Signal K.

## 5. Approve the device in Signal K

In the Signal K admin UI: **Security → Access Requests**, approve the
device (named after its hostname, e.g. `gauge-4c-stbd`; read access is
enough). The dot turns blue and the needles start to move.

## 6. Check the readings

For each page, compare the value with the Signal K server's Data Browser
(converted to the page's unit), then:

- Stop the source of one path (or unplug it): within the page's data
  timeout (10 s) its LCD readout shows dashes, and a dial parks its needle,
  the compass card stays at its last heading but dimmed, the sea horizon's
  boat levels off, dimmed.
- Restart the Signal K server: after it comes back, the pages show values
  again; values the server replays on reconnect are not shown as live.
- If the path has zones on the server, they are drawn on the dial.

## 7. Updates over WiFi

Once the device is on the network:

```bash
pio run -e p4_stbd -t upload
```

## Something wrong?

Copy the serial log from power-on to the problem; the `boot:` and `board:`
lines and any `E (...)` or `W (...)` line usually say where it stops. The
board READMEs list the known board-specific issues.
