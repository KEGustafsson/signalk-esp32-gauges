# Waveshare ESP32-S3-Touch-LCD-2.1 (SKU 28169)

Signal K steel gauges for the 2.1" round board. See the
[top-level README](../README.md) for the gauges, touch controls and setup.

| Side | OTA | First flash |
|---|---|---|
| starboard | `pio run -e s3_stbd -t upload` | `pio run -e s3_stbd_usb -t upload` |
| port | `pio run -e s3_port -t upload` | `pio run -e s3_port_usb -t upload` |

## Hardware

From the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1)
and ESP32_Display_Panel's board file `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1.h`:

| Part | Details | Used here |
|---|---|---|
| MCU | ESP32-S3R8, 8 MB OPI PSRAM, 16 MB flash, WiFi 2.4 GHz, BLE 5 | yes |
| Display | 2.1" round IPS 480×480, ST7701, 16-bit RGB (HSYNC 38, VSYNC 39, DE 40, PCLK 41, 12 MHz), init over 3-wire SPI (SCK 2, SDA 1, CS on EXIO3) | yes |
| Touch | CST820 (CST816S-compatible) on I2C SDA 15 / SCL 7, INT 16, reset EXIO2 | swipe / hold |
| IO expander | TCA9554 at 0x20: EXIO1 LCD reset, EXIO2 touch reset, EXIO3 LCD CS, EXIO4 SD CS, EXIO5/6 IMU INT, EXIO7 RTC INT, EXIO8 buzzer | via ESP32_Display_Panel |
| Backlight | GPIO6, PWM | brightness (long press) |
| IMU | QMI8658 6-axis on the shared I2C bus | not used |
| RTC | PCF85063 on the shared I2C bus | not used |
| microSD | SD_D0 42, CMD 1, CLK 2, D3 on EXIO4 (shares GPIO1/2 with the LCD init bus) | not used |
| USB | USB-C through a CH343 USB-UART (Serial) | flashing, logs |
| Battery | Li-ion connector with charger, BAT_ADC on GPIO4 | not used |
| Buttons | BOOT (GPIO0), RESET, power | BOOT = SensESP button |

The display, touch, backlight and expander are configured entirely by
ESP32_Display_Panel's built-in board file, selected in `platformio.ini` with
`-D BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1`.

Note: the Arduino ESP32-S3 variant declares an RGB status LED on GPIO48, which
on this board is an LCD data line. The firmware hands SensESP an on-screen
status LED instead (`common/src/status_led.h`), so nothing drives GPIO48.

## Build settings

- Board `esp32-s3-devkitc-1` with `qio_opi` memory (OPI PSRAM), 16 MB flash,
  `partitions_16MB.csv`: Arduino's `default_16MB.csv` (two 6.25 MB OTA
  slots) with the filesystem cut from 3.4 MB to 1 MB, which boots faster
  (see "Boot time" in the top-level README).
- The log (`ESP_LOGx`) goes to UART0 at 115200 baud and, as the core's
  secondary console (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`), to
  the S3's own USB Serial/JTAG: `pio device monitor` on either.
  `ARDUINO_USB_CDC_ON_BOOT=0` only keeps Arduino's `Serial` on UART0. On
  the board brought up, the USB-C port is the S3's own USB (a "USB Serial
  Device"); flashing and the log both work there.
- LVGL renders into one 480×20-pixel buffer in internal RAM (`kBufferLines`
  in `src/main.cpp`) and copies it into the panel's PSRAM frame buffer,
  which the panel reads all the time. `common/src/board_start.cpp` sets the
  pixel clock to 12 MHz (44 Hz refresh) and the RGB bounce buffer to 480×20
  pixels: with the board file's 16 MHz and 10 lines the picture flickered
  with noise constantly on the real board, and with 10 lines at 12 MHz it
  drifted up and still flickered. The bounce buffer's internal RAM is paid
  for with the smaller draw buffer: with a 40-line one, the largest free
  block fell to 7 KB and SensESP could not start its 8 KB connect task, so
  the unit never reconnected after a Signal K server restart (log:
  `connect worker spawn failed`). Watch the boot log's `heap` line.
- The framework is rebuilt for this board (`custom_sdkconfig` in
  `platformio.ini`), against the picture shift described below:
  - `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`: the firmware's code and read-only
    data are copied into PSRAM at boot and run from there. Flash is then
    not needed while the gauge runs, so reading or writing a file no longer
    cuts the chip off from PSRAM.
  - `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`: the RGB panel's interrupt, which fills
    the bounce buffers, lives in internal RAM and keeps running while the
    flash is busy (a file read or written, WiFi saving its settings).

  - `CONFIG_LCD_RGB_RESTART_IN_VSYNC=n` (ESP-IDF's default; the Arduino
    libraries turn it on): the fix for the shift that stays. See below.

  The first two come from ESP-IDF's RGB LCD guide for a frame buffer in
  PSRAM. The
  cost: the code takes about 1.4 MB of PSRAM (the `heap` line's PSRAM went
  from 4878 KB to 3459 KB free on the first unit). If PSRAM runs short,
  fewer pages are kept built, and a page that is not is built when shown
  (about 0.3 s). The 4" uses the stock libraries.

  Building it:
  - The first build of each env downloads ESP-IDF 5.5.5 and compiles the
    framework's libraries before the firmware: 63 minutes on the first PC
    (ESP-IDF's CMake configure ~10 min, its components, then the Arduino
    core, the libraries and the gauge). Later builds of that env with the
    same settings recompile only what changed; changing a
    `custom_sdkconfig` line rebuilds the libraries again. Each env (`s3_stbd`,
    `s3_stbd_usb`, the port ones) builds them once for itself.
  - The project has its own PlatformIO directory, `~/.pio21` (`core_dir`):
    platform, toolchain and framework. Keep its path short: on Windows,
    generating the linker script passes every framework library's full
    path on one command line, and from inside the repo
    (`../.pio-core-2.1in`) that failed after an hour with "The command line
    is too long". The rebuild
    replaces the framework's libraries in place; in the shared
    `~/.platformio` the 4"'s next build put the stock ones back ("Reinstall
    Arduino framework") and the 2.1"'s next build rebuilt them again, about
    an hour each way, and two builds at once would have overwritten each
    other. The first build in a fresh directory downloads everything again.
  - On Windows, build from PowerShell or cmd, not from Git Bash or another
    MSYS/MinGW shell: ESP-IDF's tool installer (`idf_tools.py`), which
    fetches the CMake and Ninja this build needs, refuses to run there
    ("MSys/Mingw is not supported"). PlatformIO only logs that, and the
    build then fails with `FileNotFoundError` while reading the CMake
    configuration.
- 2026-11-01: with the first two settings the picture stayed in place
  through 60 s of settings reads every 2 s and a settings save, the load
  that shifted it within half a minute on the stock libraries. It still
  shifted once at rest a few minutes later: the driver bug below, which
  turns any one late interrupt into a lasting shift. The third setting is
  its fix, under test.
- Why the shift is always exactly 20 lines and stays (ESP-IDF 5.5,
  `components/esp_lcd/rgb/esp_lcd_panel_rgb.c`): the interrupt for "a
  bounce buffer has been sent" refills a buffer chosen by a running count,
  `bb_eof_count % 2`, not by the buffer actually sent. If that interrupt is
  late by more than one buffer's time (about 0.9 ms at 12 MHz), two merge
  into one and the count falls one behind: from then on each buffer is
  filled with the other's 20 lines. At every VSYNC,
  `lcd_rgb_panel_try_restart_transmission()` resets the count and restarts
  the output when interrupts were missing, but only when
  `CONFIG_LCD_RGB_RESTART_IN_VSYNC` is off. With it on, as in the Arduino
  libraries, the output restarts every frame and the count never resets,
  so the shift stays until the unit restarts. With it off, a late
  interrupt costs one wrong frame (1/44 s).
- The picture shift, on the stock libraries: it moved up by exactly 20
  lines (one bounce buffer) and stayed so until a restart, with
  horizontal lines while it happened.
  The panel streams its picture all the time; an interrupt copies it from
  the PSRAM frame buffer into two 20-line bounce buffers in internal RAM,
  one while the other is sent. When a copy is missed, the picture is one
  buffer out of step for good. ESP-IDF's restart at every frame start
  (`CONFIG_LCD_RGB_RESTART_IN_VSYNC`, on in the stock build) does not undo
  it. On the S3, flash and PSRAM share one bus: while the flash is read or
  written (a settings file read by the web UI, a setting saved, WiFi
  connecting), neither PSRAM nor that interrupt can run, for longer than
  any bounce buffer covers. Reading the page settings every 2 s over the
  web API shifted the picture within half a minute. Heavy PSRAM traffic
  from drawing (tried: a 1 MB lookup per compass step; LVGL compiled at
  -O2) does it too. Without bounce buffers (the panel's DMA reads PSRAM
  itself) the shift cannot last, but single frames then jumped sideways
  several times a second: worse. See `docs/JOURNAL.md` (2026-11-01).

## First power-up checklist

- The gauge appears with dashes in the LCD before WiFi connects.
- Swipe left/right changes the page; a short toast shows the page name.
- A swipe down shows the IP address; it should match the one your router gave the
  device (or the static address in `secrets.h`).
- After approving the access request in Signal K, the bezel dot shows blue
  (with a brief blink each second) and the needle moves.
- Touch is read after the CST820's interrupt (GPIO16) and while a finger is
  down; if taps are ignored, check the serial log for touch read errors. If
  the CST820 does not start at all, the unit restarts once and then runs
  without touch (serial log: "Touch did not start"). Only a failed start
  counts: a reset during the start (flashing, a power dip) starts over.
- If the picture still drifts or flickers, lower `kRgbPclkHz` in
  `common/src/board_start.cpp` further.
