# Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (SKU 31522)

Signal K steel gauges for the 4" round board. See the
[top-level README](../README.md) for the gauges, touch controls and setup.

One firmware for both units (`env:p4`); a unit's side is its saved
hostname (top README, Devices).

| Upload | Command |
|---|---|
| USB | `pio run -e p4 -t upload --upload-port COM3` |
| OTA, port unit | `pio run -e p4 -t upload --upload-port port` |
| OTA, starboard unit | `pio run -e p4 -t upload --upload-port stbd` |

## Update the WiFi chip's firmware (once per board)

**Every 4" board needs this once, before it is used.** Its WiFi is a separate
chip, an ESP32-C6, with a firmware of its own (ESP-Hosted). That firmware
must be the version the gauge firmware is built for: **ESP-Hosted 2.12.13**
(Arduino-ESP32 3.3.12). Boards leave the factory with an older one. Flashing
the gauge does not update the C6.

**How to tell.** The gauge's boot log (USB serial, 115200 baud), right after
`Gauge on screen`:

| Log | Meaning |
|---|---|
| `Slave firmware version: 2.12.13` and `Versions Match!` | Up to date: nothing to do |
| `Slave firmware version: 0.0.0` or `2.12.11`, `Could not get slave firmware version: ESP_FAIL`, `Version on Host is NEWER than version on co-processor` | **Update it** |

With the old firmware WiFi still connects, so the gauge looks almost
right. But the C6 does not answer some of the P4's requests, and each one
holds the gauge for up to 5 s. The unit may fail to reach the Signal K
server, freeze for seconds at a time, or restart.

**When.** Once per board, and again whenever a platform update changes the
ESP-Hosted version (the boot log then shows the warning again).

**What it needs.** USB, and WiFi with internet access: the tool joins the
first network in `include/secrets.h` that answers and downloads the C6
image from Espressif (`espressif.github.io`). The image is checked against
the SHA-256 pinned in `tools/c6-update/src/main.cpp` before anything is
written. An interrupted update leaves the old firmware in place; run it
again.

**Steps**, over USB (`--upload-port COMn` if more than one board is
plugged in):

1. Flash the update tool. **The board then runs the tool, not the gauge**,
   until step 4:

   ```bash
   cd tools/c6-update
   pio run -t upload
   ```

   On ESP32-P4 silicon before v3.0 (the chip-id check in `docs/BRINGUP.md`)
   use `pio run -e c6_update_pre_v3 -t upload`.
2. Open the monitor and press RESET:

   ```bash
   pio device monitor
   ```

3. Wait for `DONE: the co-processor is up to date, nothing to do` after the
   restart (download, check and write take a minute or two). The full
   expected output is in [`tools/c6-update/README.md`](../tools/c6-update/README.md).
4. Flash the gauge again (the same firmware for either side):

   ```bash
   cd waveshare-esp32-p4-wifi6-touch-lcd-4c
   pio run -e p4 -t upload --upload-port COM3
   ```

   Its settings (pages, WiFi, Signal K token) are kept: they are in the
   gauge's filesystem at 16 MB (`partitions_32MB.csv`), and the tool only
   writes inside the gauge's firmware slot below it.
5. Check the boot log for `Versions Match!`.

**Units:**

| Unit | C6 firmware |
|---|---|
| 4" starboard | 2.12.13, updated 2026-11-03 (2.12.11 since 2026-10-31) |
| 4" port | 2.12.13, updated 2026-11-03 (factory firmware 0.0.0 until that morning) |

## Hardware

From the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4C),
Waveshare's ESP-IDF BSP (`waveshare/esp32_p4_wifi6_touch_lcd_xc`) and their
Arduino examples ([ESP32-P4-WIFI6-Touch-LCD-XC](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC)):

| Part | Details | Used here |
|---|---|---|
| MCU | ESP32-P4 (dual-core RISC-V up to 360/400 MHz), 32 MB PSRAM, 32 MB flash | yes |
| Wireless | ESP32-C6-MINI-1 over SDIO (esp-hosted): WiFi 6 2.4 GHz, BLE 5 | WiFi, through Arduino's `WiFi.h` |
| Display | 4" round IPS 720×720, JD9365, 2-lane MIPI-DSI at 1500 Mbps/lane, 80 MHz DPI clock, reset GPIO27, DSI PHY on LDO channel 3 | yes |
| Touch | GT9271 (GT911-compatible) on I2C SDA 7 / SCL 8, address 0x5D (0x14 alternative), reset/INT not routed | swipe / hold |
| Backlight | GPIO26, LEDC PWM, active low | brightness (long press) |
| Audio | ES8311 codec + NS4150B amplifier (speaker), ES7210 mic ADC; I2S MCLK 13, SCLK 12, LRCK 10, DOUT 9, DIN 11, PA enable GPIO53 | not used |
| microSD | 4-bit SDIO: D0–D3 GPIO39–42, CLK 43, CMD 44 | not used |
| Camera | MIPI-CSI 2-lane connector | not used |
| USB | USB-C (USB Serial/JTAG + UART), USB-A 2.0 OTG high speed | flashing, logs |
| Other | 40-pin header, RTC battery holder, BOOT and RESET buttons | not used |

ESP32_Display_Panel has no built-in entry for this board, so
[`include/esp_panel_board_custom_conf.h`](include/esp_panel_board_custom_conf.h)
describes it. Its JD9365 init table is copied verbatim from Waveshare's BSP
(720×720 variant); it is byte-identical to the 4C table in Waveshare's Arduino
examples.

## Build settings

- Board `esp32-p4_r3` (ESP32-P4 silicon v3.01 or newer, Waveshare's default). If your
  board has pre-v3 silicon (the boot log prints the chip revision), change it to
  `board = esp32-p4`.
- 32 MB flash with `partitions_32MB.csv`: two ~8 MB OTA slots that both end
  at or below 16 MB (kept there as a precaution: the precompiled libraries
  are built for 24-bit flash mapping), and a 1 MB filesystem above them for
  SensESP's settings. It used to be ~16 MB, which held touch for seconds at
  every boot: see "Boot time" in the top-level README.
- The board has two USB-C ports: the ESP32-P4's own USB (Serial/JTAG) and a
  USB-to-UART bridge. `Serial` is the USB Serial/JTAG one
  (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`, as in Waveshare's
  Arduino setup); ESP-IDF's console (and so some log lines) may come out on
  the UART one. If a port is silent, try the other.
- LVGL renders into one 720×72-pixel buffer (internal RAM, PSRAM if that is
  short) and copies it into the DPI panel's frame buffer. The flush waits for
  each copy; one that never completes restarts the unit.
- On first power-up, check that SensESP's filesystem (at 16 MB) mounts.

## First power-up checklist

- The gauge appears with dashes in the LCD before WiFi connects.
- The touch chip answers at 0x5D on most boards and 0x14 on some. If it does
  not answer, `Board::begin()` fails and the unit restarts with the other
  address (`common/src/board_start.cpp`). The address that worked is saved
  in NVS and tried first from then on, so a 0x14 board only takes the extra
  restart once. If neither address answers, the third start runs without
  touch (serial log: "Touch did not start"). A unit that keeps restarting
  with a dark screen has a display fault.
- If the backlight stays dark (or full brightness ignores the long press),
  flip `ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL` in
  `include/esp_panel_board_custom_conf.h`; Waveshare's BSP drives this pin
  inverted.
- The ESP32-C6 runs ESP-Hosted 2.12.13: `Versions Match!` in the boot log.
  If not, update it first: [Update the WiFi chip's firmware](#update-the-wifi-chips-firmware-once-per-board).
  Both boards brought up so far shipped with an older one.
- Internal DMA-capable RAM belongs to the ESP-Hosted link: it allocates its
  SDIO buffers there while data flows. With the LVGL draw buffer in internal
  RAM, 9 KB was left once WiFi was up and the Signal K connection took the
  rest; the link then stalled for good (every request to the C6 unanswered,
  no network traffic) and the loop watchdog restarted the unit every few
  seconds after it reached Signal K. The draw buffer is in PSRAM for this
  reason (`common/src/lvgl_port.cpp`).
- A single unanswered request to the C6 must not reboot the gauge. SensESP
  asks for the WiFi network name and signal strength from the main loop
  (for its status page); on this board each is a request to the C6, and the
  loop waits up to 5 s for the reply (ESP-Hosted's
  `DEFAULT_RPC_RSP_TIMEOUT`). The loop watchdog used to be 5 s too, so one
  lost reply restarted the unit (seen once, after about 6 h, with plenty of
  internal RAM free: the core dump showed the loop in `WiFi.SSID()` waiting
  for the C6). The loop watchdog is now 12 s (`kLoopWatchdogMs` in
  `common/src/gauge_app.cpp`): a lost reply freezes the picture for about
  5 s and the gauge carries on. A link that stopped answering for good
  makes every such request take 5 s; three SensESP passes of 4 s or more
  within a minute restart the unit, with `loop: SensESP held the loop over
  4 s 3 times in 60 s (a WiFi link not answering?): restarting` in the log.
  That restart skips ESP-IDF's shutdown steps: one of them tells the C6 to
  stop WiFi and waits for its answer, so with a dead link the unit hung
  there until the watchdog reset it 12 s later (seen once, after about
  19.5 h: reset reason "Task watchdog"). ESP-Hosted resets the C6 at every
  boot.
- The C6 is also asked for a heartbeat every 20 s (`common/src/link_watch.cpp`,
  as espOS does): 60 s without one restarts the unit, even when nothing on
  the P4 noticed the link die (a wedged SDIO link raises no error, and
  WiFi still looks connected). The boot log says `link: Watching the WiFi
  chip (C6)` and, a little later, `First heartbeat from the WiFi chip (C6)`.
  ESP-Hosted's own link-failure event and a C6 that restarts under a
  running link restart the unit too; its start at every boot does not.
- Arduino-ESP32 3.3.12 (ESP-Hosted 2.12.13), since 2026-11-03. On 3.3.11
  (ESP-Hosted 2.12.11) both units lost the link to their C6 every 6-7 h:
  2.12.11 does not notice a dead SDIO link by itself, 2.12.12 fixes that
  (esp-hosted-mcu #220, a P4 + C6 that wedged 43 times in 21 h on 2.12.11
  and not at all with the fix). The restart recovered each time in about
  70 s (`docs/JOURNAL.md`). 3.3.12 also leaves far more internal DMA RAM
  free: 289 KB a minute after boot, against 78 KB.
- While WiFi associates after boot, the log shows a few
  `RPC_WRAP: rpc_wifi_sta_get_ap_info: failed, status [12303]`: SensESP
  asks for the signal strength and the C6 answers at once that there is no
  access point yet. Expected; they stop once WiFi is up.
- After an unexplained restart, the boot log's reset reason and the core
  dump tell why: read the core dump partition (`coredump` at 0x1FF0000 in
  `partitions_32MB.csv`), drop its first 24 bytes, and open the rest in
  GDB with the `firmware.elf` of the build that ran (`thread apply all
  bt`). Reading it restarts the unit once. With PlatformIO's own tools,
  from this folder:

  ```bash
  pio pkg exec -p tool-esptoolpy -- esptool --chip esp32p4 read-flash 0x1FF0000 0x10000 core.bin
  tail -c +25 core.bin > core.elf
  pio pkg exec -p tool-riscv32-esp-elf-gdb -- riscv32-esp-elf-gdb .pio/build/p4_stbd/firmware.elf core.elf
  ```
- A swipe down shows the IP address; it should match the one your router gave the
  device (or the static address in `secrets.h`).
