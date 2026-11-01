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
