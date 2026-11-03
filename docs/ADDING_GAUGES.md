# Adding gauges

"More gauges" can mean four different jobs. The first two need no code and
no new build; the last two change the firmware.

| You want | What it takes | Section |
|---|---|---|
| Another page (another reading) on a unit | Web UI only | [1](#1-another-page-on-a-unit) |
| Another physical display of a board you already have | Flash the existing build, set its side and pages | [2](#2-another-unit-of-an-existing-board) |
| A new kind of page (not a dial, compass or sea horizon) | Code in `common/src`, tests, preview | [3](#3-a-new-page-style) |
| A display board this project does not support yet | A new board project | [4](#4-a-new-board) |

Changing the six factory pages a fresh unit starts with is in
[5](#5-changing-the-factory-pages).

## 1. Another page on a unit

Up to 12 pages per unit, set up in the unit's web UI, no build. An example:
the engine's exhaust temperature as page 7, on the port 2.1".

1. **Find the path.** In the Signal K server's admin UI, **Data Browser**:
   the boat's own path, e.g. `propulsion.prt_engine.exhaustTemperature`,
   and its unit (K). A path with no data there will show dashes.
2. **Open the unit's web UI**: `http://gauge-21-port.local`, or the IP the
   info overlay shows (swipe down on the gauge). Log in with
   `SECRET_ADMIN_USER` / `SECRET_ADMIN_PASSWORD` from `secrets.h`.
3. **Configuration → Pages**: **Number of pages** 6 → 7, **Save**, and
   restart when the web UI asks. Pages are swiped through in order; the new
   one is last, a dial with no path (dashes).
4. **Configuration → Page 7**, the settings ([PAGES.md](PAGES.md#the-settings-of-a-page)
   explains each):

   | Setting | Example | Note |
   |---|---|---|
   | Style | Dial | Compass for a heading, Sea horizon for `navigation.attitude` |
   | Title | `Exhaust` | On the dial face |
   | Signal K path | `propulsion.prt_engine.exhaustTemperature` | Exactly as in the Data Browser |
   | Display unit | °C | Must measure what the path does (K: °C, °F or K) |
   | Set the scale by hand | off | Off: the server's `displayScale` if it has one, else the minimum / maximum below |
   | Scale minimum / maximum | 0 / 600 | In the display unit |
   | Show Signal K zones | on | The server's zones for the path |
   | Decimals | 0 | In the LCD readout |
   | Data timeout | 10 s | Longer for a slow sender, e.g. a tank every 30 s |

   **Save**, restart when asked.
5. **Zones and scale on the server** (optional): in the Signal K admin UI,
   the path's metadata (Data Browser → the path → meta): `zones` (e.g.
   `warn` from 723.15 K, `alarm` from 773.15 K, always in SI) and
   `displayScale`. Every display that reads the path, this unit included,
   draws them; the unit picks up a change without a restart
   ([PAGES.md](PAGES.md#zones-and-scale-from-the-signal-k-server)).
6. **Check on the gauge**: swipe to the new page. The needle and LCD follow
   the value; dashes mean no value within the data timeout (wrong path, no
   data, or a slow source).

**Another position**: **Swap with page** in a page's card moves it (e.g.
page 7 with page 2). **The same page on other units**: repeat on each, or
copy a unit's pages to another with `tools/copy-pages` (below).

## 2. Another unit of an existing board

Each board has one firmware for all its units, so a new 4" or 2.1" display
needs no new build: flash the firmware already built, then tell the unit
which it is.

1. **4" only: update the WiFi chip's firmware first** (ESP32-C6, once per
   board): [4" README](../waveshare-esp32-p4-wifi6-touch-lcd-4c/README.md#update-the-wifi-chips-firmware-once-per-board).
2. **Flash over USB** from the board's folder (`pio device list` shows the
   port). If the board's env is built, nothing compiles:

   ```bash
   cd waveshare-esp32-s3-touch-lcd-2.1
   pio run -e s3 -t upload --upload-port COM9
   ```

   It joins WiFi from `secrets.h` and comes up as the board's starboard unit
   (`gauge-21-stbd`).
3. **What it is**: web UI, **Configuration → Unit**: the role (Starboard,
   Port or General), the hostname and the display name; save, restart.
   The role picks the factory pages; the hostname follows it while it is a
   built-in one (`gauge-21-port`).
4. **Approve it in Signal K**: server admin UI, **Security → Access
   Requests**, the request named after the unit; read access is enough.
5. **Its pages**: as in section 1, or copied from a unit that has them,
   with the engine swapped for the other side
   ([tools/copy-pages](../tools/copy-pages/README.md)):

   ```bash
   python tools/copy-pages/copy_pages.py --from gauge-21-stbd.local --to gauge-21-port.local \
       --replace stb_engine=prt_engine --replace "STB Engine=PRT Engine" --restart
   ```

   It writes each card with the target's own `_rev` (the gauge refuses a
   save from an out-of-date form) and restarts the target when needed.
6. **`secrets.h`**: `SECRET_OTA_HOST_<ENV>_STBD` / `_PORT` hold each side's
   address. They are used for over-the-air updates by side
   (`pio run -e s3 -t upload --upload-port port`) and, with
   `SECRET_USE_STATIC_IP 1`, as the unit's static IP.

After that, updates go over WiFi: `--upload-port port` or `stbd`, or the
unit's address.

**A unit for something else than an engine** (say a navigation display at
the helm): in its Unit card choose the role **General**, a hostname of its
own (e.g. `gauge-4c-nav`) and a display name (e.g. `Helm NAV`), save and
restart. Its factory pages are then speed, heading, depth, wind, heel and
battery (PAGES.md, Factory pages), all changeable. It has no address in
`secrets.h` and gets one by DHCP whatever `SECRET_USE_STATIC_IP` says.
Update it by its name, `--upload-port gauge-4c-nav.local`, or give it a
short one: `#define SECRET_OTA_HOST_P4_NAV "gauge-4c-nav.local"` in
`secrets.h`, then `--upload-port nav`. A board can have any number of
units; only their hostnames must differ.

## 3. A new page style

A style is a kind of page (today: Dial, Compass, Sea horizon). A new one
touches these files, in this order. Keep the rules of
[AGENTS.md](../AGENTS.md): host-safe logic with tests, UI code that builds
on the host, and never a stale value shown as live.

| File | What to add |
|---|---|
| `common/src/gauge_defs.h` | A value in `enum class GaugeKind` |
| `common/src/gauge_defs.cpp` | Its template in `style_template()`: default title, path, display unit, range, ticks, listen delay. Paths and ranges live only here. |
| `common/src/page_rules.cpp` | Its name in `page_style_name()` and in the list `parse_page_style()` walks; its name in the **Style** dropdown of `page_schema()`; which fields its card shows (`page_schema()` shows the unit, scale and zones only for a dial) |
| `common/src/gauge_resolve.cpp` | Only if settings and metadata combine differently for it (as for the compass) |
| `common/src/sk_data.cpp` | How its value is read. A number: the existing `StrictFloatListener` path. An object (like `navigation.attitude`): a listener in `sk_listeners.h` and a parser in `sk_parse.h` that accepts only real JSON numbers (`sk_number()`), with tests |
| `common/src/ui/<style>_gauge.{h,cpp}` | The page: a class derived from `GaugePage` (`ui/gauge_page.h`) with `update(value, now_ms)`. Draw the static face once into the canvas; redraw only the moving part, invalidating only the area that changes (see `compass_gauge.cpp`). Dim or park it when the value is stale (`GaugeValue`, `set_stale_look()` in `gauge_page.h`). No Arduino or SensESP here. |
| `common/src/ui/gauge_page.cpp` | A `case` in `create_page()` |
| `tools/preview/preview.cpp` | A sample page in `preview_pages()` |
| `tests/` | `test_page_rules.cpp` (name, parsing, schema), `test_resolve.cpp`, `test_sk_parse.cpp` for a new parser, `test_factory.cpp` if the template matters |
| `docs/PAGES.md` | The style and its settings, for users |

Then:

1. Host tests: `cmake -S tests -B build/tests && cmake --build build/tests -j && build/tests/gauge_tests`.
2. Preview at both sizes and **look at the images**:
   `cmake --build build/preview -j && build/preview/preview 480 && build/preview/preview 720`
   (`out/`). `preview <size> bench` times a redraw in the boards' strip
   heights.
3. Build both boards (`pio run -e p4`, `pio run -e s3`), flash one unit of
   each, and watch the page live, on the 2.1" in particular: its panel
   shares the PSRAM bus with drawing, and memory-heavy drawing shifts the
   picture (AGENTS.md, Gotchas). Check the `loop: Slowest pass` log line.
4. An entry in `docs/JOURNAL.md`.

## 4. A new board

A board is a project folder next to the two existing ones; the gauges
themselves (`common/src`) are shared.

1. **Project**: copy the closer existing one
   (`waveshare-esp32-p4-wifi6-touch-lcd-4c` or
   `waveshare-esp32-s3-touch-lcd-2.1`) and adapt:
   - `platformio.ini`: `board`, flash and PSRAM settings, `lib_deps`
     (keep SensESP and `SignalKGauges=symlink://../common`), and **one env**
     for both sides, named for the board (e.g. `c6`). No per-side env and no
     side `-D` flag: each env compiles everything.
   - `src/main.cpp`: start the display and touch (`start_board()`,
     `lvgl_port_init()` with a draw buffer that fits its RAM), then
     `gauge_app_begin({"<id>", "<label>", SECRET_OTA_HOST_<ENV>_STBD,
     SECRET_OTA_HOST_<ENV>_PORT, button_pin, set_brightness})`. `<id>` goes
     into the hostname (`gauge-<id>-port`), `<label>` into the unit name.
   - `include/lv_conf.h`, and a panel description if ESP32_Display_Panel has
     no built-in entry (the 4"'s `esp_panel_board_custom_conf.h`).
2. **Addresses**: `SECRET_OTA_HOST_<ENV>_STBD` and `_PORT` in
   `include/secrets.example.h` (committed, the mDNS names as defaults) and
   in your `include/secrets.h`.
3. **Dial size**: the UI scales to the panel radius (`Geometry` in `ui/draw_utils.h`); render the
   preview at the new width (`build/preview/preview <width>`) and look.
4. **CI**: the board in the `firmware-build` matrix of
   `.github/workflows/ci.yml` (`project`, `env`).
5. **Docs**: the board table in `AGENTS.md` and `README.md`, a board
   README (hardware, build settings, first power-up checklist), and the
   bring-up in `docs/BRINGUP.md`; then bring it up as that guide says
   before trusting a reading.

## 5. Changing the factory pages

The six pages a fresh unit (or **Reset to factory settings**) starts with
are `kPortPages` / `kStbdPages` in `common/src/gauge_defs.cpp`, by side. Keep
both lists the same length (a `static_assert` checks it), keep paths and
ranges only there, and update `tests/test_factory.cpp` and the table in
[PAGES.md](PAGES.md#factory-pages). Units already set up keep their pages;
only fresh or reset ones get the new defaults. The boat's own path names
(e.g. `stb_engine`) belong in each unit's web UI, not here: the factory
pages stay generic (`port`, `starboard`).
