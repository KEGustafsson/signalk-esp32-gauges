# Setting up the gauge pages

Every device shows one page at a time; swipe left or right to move between
them. You decide how many pages there are, and what each one shows, in the
device's web UI, much like gauge widgets in SKip. Nothing needs to be
reflashed.

- [The three page styles](#the-three-page-styles)
- [Opening the web UI](#opening-the-web-ui)
- [Add a page](#add-a-page)
- [Remove a page](#remove-a-page)
- [Change a page](#change-a-page)
- [Reorder pages](#reorder-pages)
- [The settings of a page](#the-settings-of-a-page)
- [Zones and scale from the Signal K server](#zones-and-scale-from-the-signal-k-server)
- [Examples](#examples)
- [Factory pages](#factory-pages)
- [Troubleshooting](#troubleshooting)

## The three page styles

| Style | What you see | Use it for |
|---|---|---|
| **Dial** | A steel dial with needle, numbered scale, coloured zones and an LCD readout | Anything that is a single number: engine rpm, temperatures, speed, pressure, depth, voltage, tank level, wind speed... |
| **Compass** | A rotating compass card with an LCD heading; °T, or °M when the path is a magnetic one | A heading, e.g. `navigation.headingTrue` or `navigation.headingMagnetic` |
| **Sea horizon** | A boat symbol on a level horizon; heel in degrees PORT/STBD on a ±40° scale, and trim | `navigation.attitude` (its `roll` and `pitch`) |

## Opening the web UI

1. Make sure your phone or computer is on the same network as the device.
2. Open the device in a browser by its name, `http://<hostname>.local/` —
   for example `http://gauge-4c-stbd.local/`. If the name does not work,
   tap the device's screen: the info box shows its IP address, and
   `http://<that address>/` works too.
3. Log in if asked (the admin user and password from `secrets.h`).
4. Go to **Configuration**. Near the top is the **Pages** card, then one
   card per page: **Page 1**, **Page 2**, ...

Every card has its own **Save** button. Page settings take effect after a
restart: once you have saved, a **Restart** button appears at the top of the
web UI. You can save several cards first and restart once.

## Add a page

1. In the **Pages** card, raise **Number of pages** by one (up to 12) and
   **Save**.
2. Press **Restart**. When the web UI is back, the new **Page n** card is at
   the end of the list.
3. In that card, pick the **Style**, enter the **Signal K path** and
   whatever else the page needs (see [the settings](#the-settings-of-a-page)),
   then **Save** and **Restart** again.

The new page is the last one in the swipe order. Until it has a path, a
new dial shows dashes.

## Remove a page

Pages are swiped through in card order, Page 1 to Page n, so the
**Number of pages** always removes pages from the end.

- **The last page:** lower **Number of pages** by one, **Save**,
  **Restart**.
- **A page in the middle:** move every later page up one card first (see
  [Reorder pages](#reorder-pages)), so the page you no longer want ends up
  last; then lower the number.

A removed page's settings are kept on the device: raise the number again
and it comes back as it was.

## Change a page

Open its **Page n** card, change what you need, **Save**, **Restart**. To
see which page is which, tap the device's screen on that page: the info box
shows the Signal K path it reads.

Changing the **Style** starts the page from that style's defaults: the
title, path, unit, scale and decimals you leave as they were are reset (a
compass to `navigation.headingTrue`, a sea horizon to `navigation.attitude`,
a dial to no path, unit SI, 0–100), while anything you change in the same
save is kept. So to turn a page into a fuel gauge, pick **Dial** and type
the tank's path, unit and scale before pressing **Save**.

Changing only the **Display unit** of a dial converts its scale: 0–120 °C
becomes 0–250 °F, 0–30 kn becomes 0–60 km/h. A unit that measures something
else gives 0–100; set the scale you want in the same save. When the path
changes in the same save (a new page, for example), the scale is left as it
is — set it to suit the new path.

A card holds everything its page shows, so the page does not depend on which
card it is in: settings copied from one card to another give the same page.

## Reorder pages

The order is the card order, so pages are reordered by copying settings
between cards: to swap Page 2 and Page 4, note the settings of both, then
enter Page 4's settings into the Page 2 card and Page 2's into the Page 4
card. **Save** both and **Restart** once.

## The settings of a page

| Setting | For | What it does |
|---|---|---|
| **Style** | all | Dial, Compass or Sea horizon. |
| **Title** | all | The text on the dial face, e.g. `PRT Engine`. Leave it empty for the style's default (`Gauge`, `HEADING`, `HEEL`). |
| **Signal K path** | all | The path the page reads, e.g. `propulsion.port.revolutions`. Empty: a compass reads `navigation.headingTrue`, a sea horizon `navigation.attitude`; a dial shows dashes. The Signal K server's **Data Browser** lists every path your boat has. |
| **Display unit** | dial | The unit shown on the dial, converted from the Signal K unit of the path (see the table below). **SI** shows the value exactly as Signal K sends it and labels it with the server's unit. |
| **Set the scale by hand** | dial | Off: the dial's range comes from the Signal K server (`displayScale`) when it sends one, else from **Scale minimum / maximum**. On: always **Scale minimum / maximum**. |
| **Scale minimum / maximum** | dial | The dial's range in the display unit, e.g. 0 and 3000 for rpm. The tick marks follow the range. |
| **Show Signal K zones** | dial | Draws the coloured zones the Signal K server holds for the path. Off: no zones. |
| **Decimals** | all | Digits after the decimal point in the LCD readout, 0–3. |
| **Data timeout [s]** | all | How old a value may be before the page shows dashes (default 10 s). A slow source, e.g. a tank sender every 30 s, needs a longer timeout. |

Signal K sends every value in an SI unit. A dial converts it to the display
unit you pick; the unit must measure the same thing as the path:

| Signal K unit | Display units |
|---|---|
| Hz (revolutions) | rpm, Hz |
| K (temperature) | °C, °F, K |
| m/s (speed) | kn, km/h, mph, m/s |
| rad (angle) | ° |
| Pa (pressure) | bar, kPa, psi |
| m (depth, distance) | m, ft |
| ratio (0–1: tank level, load) | % |
| V, A, W | V, A, W |
| anything | SI (as sent) |

## Zones and scale from the Signal K server

The coloured bands on a dial are not set on the device: they are the
path's **zones** in the Signal K server's metadata, so every display on the
boat (this one, SKip, others) shows the same limits. Colours follow SKip:

| Zone state | Colour |
|---|---|
| nominal | blue |
| alert | yellow |
| warn | orange |
| alarm, emergency | red |
| normal | (not drawn) |

Set them on the server, in the path's metadata: in the Signal K admin UI's
**Data Browser**, open the path's metadata, or edit the server's
`baseDeltas.json`. Values are in the path's **SI unit** — for rpm that is
Hz (rpm ÷ 60), for temperatures kelvin (°C + 273.15). For example, a
warning from 2500 rpm and an alarm from 2800 rpm, on a 0–3000 rpm dial:

```json
[
  {
    "context": "vessels.self",
    "updates": [
      {
        "meta": [
          {
            "path": "propulsion.port.revolutions",
            "value": {
              "units": "Hz",
              "displayScale": { "lower": 0, "upper": 50 },
              "zones": [
                { "lower": 41.667, "upper": 46.667, "state": "warn" },
                { "lower": 46.667, "state": "alarm" }
              ]
            }
          }
        ]
      }
    ]
  }
]
```

- A zone without `lower` starts at the bottom of the dial; one without
  `upper` runs to the top.
- `displayScale` sets the dial's range (when **Set the scale by hand** is
  off), also in SI.
- Zones and scale changed on the server reach the dial when the server
  sends them — at the latest when the device reconnects — without a
  restart.
- If the server's `units` for the path do not match the display unit (for
  example °C chosen for a path in Hz), the dial draws no zones and ignores
  `displayScale`, rather than drawing them in the wrong place.

## Examples

**Engine rpm** — Style **Dial**, Title `PRT Engine`, path
`propulsion.port.revolutions`, Display unit **rpm**, Decimals 0. Zones and
range from the server, as above, or Set the scale by hand: 0 to 3000.

**Coolant in Fahrenheit** — **Dial**, Title `Coolant`, path
`propulsion.port.temperature` (or `coolantTemperature`, whichever your
engine gateway sends), Display unit **°F**, scale by hand 100 to 250.

**Fuel tank** — **Dial**, Title `Fuel`, path
`tanks.fuel.0.currentLevel`, Display unit **%**, scale by hand 0 to 100,
Decimals 0, Data timeout 60 s. On the server, an `alarm` zone up to 0.1 and
a `warn` zone from 0.1 to 0.25 (the level is a ratio, 0–1).

**House battery** — **Dial**, Title `Battery`, path
`electrical.batteries.house.voltage`, Display unit **V**, scale by hand 10
to 16, Decimals 1.

**Depth** — **Dial**, Title `Depth`, path
`environment.depth.belowTransducer`, Display unit **m** (or **ft**), scale
by hand 0 to 50, Decimals 1.

**Apparent wind speed** — **Dial**, Title `Wind`, path
`environment.wind.speedApparent`, Display unit **kn**, scale by hand 0 to
40.

**Magnetic heading** — Style **Compass**, path
`navigation.headingMagnetic`. The readout then says °M.

**Heel and trim** — Style **Sea horizon**, path empty (it reads
`navigation.attitude`).

## Factory pages

A freshly flashed device has six pages. The engine pages read the side the
device was built for (`port` or `starboard`):

| Page | Style | Title | Signal K path | Unit | Range |
|---|---|---|---|---|---|
| 1 | Dial | PRT / STB Engine | `propulsion.<side>.revolutions` | rpm | 0–3000 |
| 2 | Dial | Speed | `navigation.speedOverGround` | kn | 0–30 |
| 3 | Compass | HEADING | `navigation.headingTrue` | °T | 0–360 |
| 4 | Dial | Coolant | `propulsion.<side>.temperature` | °C | 0–120 |
| 5 | Dial | Oil | `propulsion.<side>.oilTemperature` | °C | 0–120 |
| 6 | Sea horizon | HEEL | `navigation.attitude` | ° | heel ±40 |

Pages 7–12 start as dials with no path. To put a page back to its factory
setting, enter the values from this table in its card. Erasing the device's
flash (see the main README) resets every page, and every other setting,
at once.

## Troubleshooting

| What you see | Why, and what to do |
|---|---|
| A page shows dashes | No value has arrived recently: the path has no data (check it in the server's Data Browser), the path is misspelled, the dial has no path yet, or the source is slower than the **Data timeout**. Dashes are deliberate: the device never shows a number it cannot confirm. |
| A dial has no coloured zones | The server has no zones for that path, **Show Signal K zones** is off, or the display unit does not match the server's unit for the path. |
| The needle sits at the end of the scale | The value is outside the range: widen the scale, or check the display unit (e.g. rpm chosen for a path that is not in Hz). |
| A new page card is missing | Raise **Number of pages**, save, and restart: the cards appear after the restart. |
| A change did nothing | Page settings apply after **Restart**. |
| The web UI refuses a save | A setting is out of range: the scale minimum must be below its maximum; decimals are 0–3; the data timeout is 1–600 s. |
