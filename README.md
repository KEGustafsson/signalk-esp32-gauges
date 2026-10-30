# signalk-esp32-gauges

Classic steel-style Signal K instrument gauges for Waveshare round touch
displays. Each device shows one page at a time and you swipe between them.
The pages are yours to define in the web UI, like gauge widgets in SKip: you
choose how many to swipe through (1–12), and each is a steel dial (rpm, temperature, speed, any number), a compass
card or a sea-horizon heel/trim indicator, with its own title, Signal K path,
unit and scale; the coloured zones come from the Signal K server. Data comes
from a Signal K server through
[SensESP](https://github.com/SignalK/SensESP); the look follows the
"Classic Steel" gauges of [SKIP](https://github.com/halos-org/skip).

![Gauge pages, rendered at 2.1" resolution](docs/preview-2.1in.png)

*The factory pages (both sides) and two example user-defined dials, with
sample values and sample Signal K zones, rendered on a PC by
[`tools/preview`](tools/preview/) from the firmware's own drawing code.*

| Folder | Hardware | Display |
|---|---|---|
| [`waveshare-esp32-p4-wifi6-touch-lcd-4c/`](waveshare-esp32-p4-wifi6-touch-lcd-4c/) | Waveshare **ESP32-P4-WIFI6-Touch-LCD-4C** (SKU 31522) | 4" round 720×720, MIPI-DSI |
| [`waveshare-esp32-s3-touch-lcd-2.1/`](waveshare-esp32-s3-touch-lcd-2.1/) | Waveshare **ESP32-S3-Touch-LCD-2.1** (SKU 28169) | 2.1" round 480×480, RGB |
| [`common/`](common/) | Shared library: gauge pages, Signal K data, LVGL port | — |
| [`tools/preview/`](tools/preview/) | Renders the gauge pages on a PC | — |

Each hardware folder is its own PlatformIO project. Both build the same gauge
code from `common/`, scaled to the panel's size.

## Devices

Each board builds as a starboard or a port device:

| Device | Side | Project | OTA env | First flash (USB) |
|---|---|---|---|---|
| 4" | starboard | `waveshare-esp32-p4-wifi6-touch-lcd-4c` | `p4_stbd` | `p4_stbd_usb` |
| 4" | port | `waveshare-esp32-p4-wifi6-touch-lcd-4c` | `p4_port` | `p4_port_usb` |
| 2.1" | starboard | `waveshare-esp32-s3-touch-lcd-2.1` | `s3_stbd` | `s3_stbd_usb` |
| 2.1" | port | `waveshare-esp32-s3-touch-lcd-2.1` | `s3_port` | `s3_port_usb` |

The side only picks the engine of the factory pages (below); every page can
be changed in the web UI.

## Pages

A page is one of three styles:

| Style | Shows | Reads |
|---|---|---|
| **Dial** | Steel dial with needle, scale and LCD readout; zones from Signal K | any numeric path, e.g. rpm, temperature, speed, pressure, depth, voltage |
| **Compass** | Rotating compass card with LCD heading, °T (°M when the path is magnetic) | a heading, e.g. `navigation.headingTrue` |
| **Sea horizon** | Boat symbol on a level horizon; heel ° PORT/STBD on a ±40° scale, trim ° | `navigation.attitude` (`roll`, `pitch`) |

### Setting up the pages

**[docs/PAGES.md](docs/PAGES.md) is the full guide**: adding, removing,
changing and reordering pages, every setting, zones on the Signal K server,
examples and troubleshooting. In short:

In the SensESP web UI (**Configuration**), the **Pages** card sets the
**Number of pages** to swipe through, 1 to 12 (6 on a fresh device). Each of
those pages then has its own card, **Page 1** to **Page n**, in swipe order,
set up the way SKip sets up a gauge widget. Save, then restart the device
(a new page's card appears after the restart):

- **Style** -- Dial, Compass or Sea horizon.
- **Title** -- the text on the dial face; empty for the style's default.
- **Signal K path** -- the path the page reads. Empty: a compass or sea
  horizon reads its usual path; a dial shows dashes until a path is set.
- **Display unit** (dial) -- converted from the path's Signal K (SI) unit:
  rpm/Hz, °C/°F/K, kn/km/h/mph/m/s, °, bar/kPa/psi, m/ft, %, V, A, W; or
  **SI** for the value as sent, labelled with the server's unit.
- **Set the scale by hand**, **Scale minimum / maximum** (dial) -- the
  dial's range; off, the server's `displayScale` for the path takes
  precedence when it sends one. Changing only the unit (same path) converts
  the range.
- **Show Signal K zones** (dial) -- see below.
- **Decimals** -- 0–3, for the LCD readout.
- **Data timeout [s]** -- a value older than this shows dashes (default 10).

### Factory pages

A freshly flashed device swipes through these six; pages 7–12, once the
number of pages reaches them, start as dials with no path.

| Page | Style | Title | Signal K path | Unit | Range |
|---|---|---|---|---|---|
| 1 | Dial | PRT / STB Engine | `propulsion.<side>.revolutions` | rpm | 0–3000 |
| 2 | Dial | Speed | `navigation.speedOverGround` | kn | 0–30 |
| 3 | Compass | HEADING | `navigation.headingTrue` | °T | 0–360 |
| 4 | Dial | Coolant | `propulsion.<side>.temperature` | °C | 0–120 |
| 5 | Dial | Oil | `propulsion.<side>.oilTemperature` | °C | 0–120 |
| 6 | Sea horizon | HEEL | `navigation.attitude` | ° | heel ±40 |

`<side>` is `port` or `starboard`, from the device's env. Coolant reads
`propulsion.<side>.temperature`, the "engine temperature" an NMEA 2000
engine gateway publishes (PGN 127489, through signalk-server's
n2k-signalk); point it at `coolantTemperature` if your source uses that
key. The sea horizon also works with a heel-only source: the trim LCD then
shows dashes. The factory pages are in
[`common/src/gauge_defs.cpp`](common/src/gauge_defs.cpp).

### Zones come from the Signal K server

The coloured bands on a dial are the path's zones as the server holds them
(`meta.zones`), in SKip's colours: nominal blue, alert yellow, warn orange,
alarm and emergency red; `normal` is not drawn. Without zones on the server
a dial shows none, and none either when the server's `units` for the path
are not what the chosen display unit converts from (for example °C on a
path in Hz). Set them in the Signal K server's admin UI (Data Browser
→ the path → Meta), or in `baseDeltas.json`, in SI units, for example for
an engine (Hz; warn from 2500 rpm, alarm from 2800 rpm):

```json
"zones": [
  { "lower": 41.667, "upper": 46.667, "state": "warn" },
  { "lower": 46.667, "state": "alarm" }
]
```

The gauge subscribes with metadata, so zones changed on the server redraw
the dial when the server sends them (at the latest on the next reconnect),
without a restart. A `displayScale`
(`{"lower": 0, "upper": 50}`, SI) sets the dial's range the same way.

### Only confirmed data

The gauges never show a number nobody vouches for:

- A value that is not a number (for example `null` from a source that has
  lost its data) is never shown as 0: the gauge blanks within about 3 s,
  unless another source of the same path keeps sending valid values.
- A value older than the page's data timeout (10 s unless set) is shown
  as dashes. A dial needle parks at its
  minimum, the compass card stays where it was but dimmed, and the sea
  horizon's boat symbol levels off, dimmed.
- After every (re)connect to Signal K the first value of each path, and
  anything within 1 s after it, is ignored: the server replays the last
  value it holds from each source, however old. Nothing is taken while the
  connection is down.

## Touch

- **Swipe left / right**: next / previous gauge (wraps around).
- **Tap**: info box with the device, its IP address, the Signal K connection
  and the Signal K path the current page listens to. Tap again to close it.
- **Long press**: backlight 100 → 60 → 30 → 10 %.

The page and brightness are remembered across restarts. If the touch
controller does not start, the device restarts and tries again (on the 4" with
the GT911's other address), and after that runs without touch rather than
not at all, showing the remembered page. Every restart tries touch again.

Pages are kept once built, and the others are built in the background a few
seconds after boot, while the screen is not being touched, so a swipe only
has to show a page. (If PSRAM runs short, the device falls back to building
one page at a time.)

### Boot time

The gauge appears as soon as the display is up, but touch only answers once
SensESP has started (`setup()` has returned). Most of that time went into
SensESP reading its settings: each lookup walks the whole SPIFFS partition,
so the filesystem is now 1 MB on both boards (it was ~16 MB on the 4" and
3.4 MB on the 2.1"). The serial log shows where boot time goes (`boot:`
lines, in ms since start) and warns when anything holds the main loop for
150 ms or more (`loop:` lines).

The new partition table only takes effect with a USB flash (OTA uploads do
not change it), and it resets SensESP's settings once: WiFi comes back from
`secrets.h`, but Signal K paths changed in the web UI go back to their
defaults and the device has to be approved again in the Signal K admin UI
(**Security → Access Requests**). Erasing first is cleanest; use each device's
`_usb` env in its board project, for example from the repository root:

```bash
pio run -d waveshare-esp32-p4-wifi6-touch-lcd-4c -e p4_stbd_usb -t erase
```

```bash
pio run -d waveshare-esp32-p4-wifi6-touch-lcd-4c -e p4_stbd_usb -t upload
```

The small dot at the
bottom of the bezel is SensESP's status LED: its colour and blink pattern show
the WiFi and Signal K state, and it shows blue, with a brief blink each
second, when connected.

## Setup

1. Install [PlatformIO](https://platformio.org/install) (VS Code extension or
   `pip install platformio`).
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the WiFi,
   the Signal K server, an OTA password (letters, digits and
   `. _ ~ + = @ : , / -` only: it goes on a shell command line) and a web UI
   admin password.
   `secrets.h` is gitignored; it is shared by both projects and by the OTA
   upload script. Each device's address defaults to its mDNS name (for
   example `gauge-4c-stbd.local`); put an IP address there instead if your
   computer does not resolve `.local` names. Set `SECRET_USE_STATIC_IP` to `1`
   (with IP addresses, netmask, gateway and DNS filled in) if you want each
   device to configure its address statically instead of using DHCP. These values only seed a
   device with no saved settings: once WiFi or the Signal K server has been
   saved on the device, its saved values win.
3. First flash of each device over USB, for example a 4" starboard one:

   ```bash
   cd waveshare-esp32-p4-wifi6-touch-lcd-4c
   ```

   ```bash
   pio run -e p4_stbd_usb -t upload
   ```

4. In the Signal K server admin UI, approve the device's access request
   (**Security → Access Requests**; the device is named after its hostname, e.g.
   `gauge-4c-stbd`). Read access is enough.
5. After that, update over WiFi:

   ```bash
   pio run -e p4_stbd -t upload
   ```

   `scripts/ota_auth.py` (adapted from
   [drive-remote-controller](https://github.com/KEGustafsson/drive-remote-controller))
   takes the OTA password and the device's address from `include/secrets.h`, so
   neither is written into `platformio.ini`; `scripts/ota_quiet.py` keeps
   espota from echoing the password into the upload log.

SensESP's setup access point (named after the device's hostname) follows
`secrets.h`: with `SECRET_AP_PASSWORD` set it uses that password; with the
boat's WiFi set and no AP password it is off; with nothing configured it
comes up with SensESP's default password `thisisfine`, so a device flashed
without a `secrets.h` can be set up from the web UI at `192.168.4.1`. With
`SECRET_ADMIN_PASSWORD` set, the web UI asks for that login on the boat's
network (the device restarts once after the login changes, to apply it);
requests over the setup access point are not asked to log in, so there the
access point password is the protection.

The access point settings in `secrets.h` only seed a device that has no saved
WiFi settings yet: once WiFi has been saved (in the web UI, or on first
boot from `secrets.h`), SensESP keeps its saved access point settings across
reflashes. To apply new ones, erase the flash and flash again over USB, which
also clears every other setting (the Signal K token too):

```bash
pio run -e p4_stbd_usb -t erase
```

```bash
pio run -e p4_stbd_usb -t upload
```

The same erase is the way back to a device that cannot be reached at all
(wrong saved WiFi and no access point): the 4" has no button for SensESP's
reset; on the 2.1", holding BOOT for 5 s resets SensESP's settings.

The devices also reboot rather than freeze: the main loop runs under a 5 s
watchdog (paused during OTA uploads), and a failed display start or a stuck
screen update restarts the device.

## How it is built

- **Display and touch**: [ESP32_Display_Panel](https://github.com/esp-arduino-libs/ESP32_Display_Panel),
  Espressif's driver library that Waveshare's own Arduino examples use. The
  2.1" uses its built-in board file; the 4" uses a custom board file made from
  Waveshare's ESP-IDF BSP and Arduino examples.
- **Graphics**: LVGL 9.3. The static dial face (chrome bezel, scale, numbers)
  is drawn once into a canvas, including the sea horizon, which is always
  level; only the needle, compass card, boat symbol, heel pointer and LCD
  readouts are redrawn as values change.
- **Data**: SensESP (main branch) subscribes to the Signal K paths over WebSocket. SensESP
  and LVGL run on the same Arduino loop task, so they share data without locks.
- **Framework**: Arduino-ESP32 3.3.11 through the pioarduino platform.

## Status

The firmware compiles for both boards, but it has not yet been run on the
hardware. CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) builds both
boards (as their starboard devices), renders every gauge page on the host, and checks that no credential
is committed, on every pull request and every push to `main`. Working rules for contributors
(and AI agents) are in [AGENTS.md](AGENTS.md). Things to check on first power-up are listed in each board folder's
README.
