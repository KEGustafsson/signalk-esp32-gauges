# Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (SKU 31522)

Signal K steel gauges for the 4" round board. See the
[top-level README](../README.md) for the gauges, touch controls and setup.

| Side | OTA | First flash |
|---|---|---|
| starboard | `pio run -e p4_stbd -t upload` | `pio run -e p4_stbd_usb -t upload` |
| port | `pio run -e p4_port -t upload` | `pio run -e p4_port_usb -t upload` |

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
- The ESP32-C6 must run the ESP-Hosted firmware of Arduino-ESP32 3.3.11
  (2.12.11). A boot log with `Slave firmware version: 0.0.0` or `Version on
  Host is NEWER than version on co-processor` needs `tools/c6-update` once:
  with an older C6 firmware WiFi connects, but the C6 does not answer some
  requests (the first board brought up shipped with such a firmware).
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
