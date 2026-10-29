# Waveshare ESP32-S3-Touch-LCD-2.1 (SKU 28169)

Signal K steel gauges for the 2.1" round board. See the
[top-level README](../README.md) for the gauges, touch controls and setup.

| Unit | Side | Address | OTA | First flash |
|---|---|---|---|---|
| 2.1" #1 | starboard | 192.168.220.132 | `pio run -e s3_stbd -t upload` | `pio run -e s3_stbd_usb -t upload` |
| 2.1" #2 | port | 192.168.220.133 | `pio run -e s3_port -t upload` | `pio run -e s3_port_usb -t upload` |

## Hardware

From the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1)
and ESP32_Display_Panel's board file `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1.h`:

| Part | Details | Used here |
|---|---|---|
| MCU | ESP32-S3R8, 8 MB OPI PSRAM, 16 MB flash, WiFi 2.4 GHz, BLE 5 | yes |
| Display | 2.1" round IPS 480×480, ST7701, 16-bit RGB (HSYNC 38, VSYNC 39, DE 40, PCLK 41, 16 MHz), init over 3-wire SPI (SCK 2, SDA 1, CS on EXIO3) | yes |
| Touch | CST820 (CST816S-compatible) on I2C SDA 15 / SCL 7, INT 16, reset EXIO2 | swipe / tap / hold |
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
  `default_16MB.csv` (two 6.25 MB OTA slots + filesystem).
- Serial is the CH343 UART (`ARDUINO_USB_CDC_ON_BOOT=0`), 115200 baud:
  `pio device monitor`.
- LVGL renders into one 480×40-pixel buffer in internal RAM (PSRAM if that
  is short) and copies it into the panel's PSRAM frame buffer. The RGB bounce buffer (480×10 pixels)
  comes from the board file and keeps the picture stable while WiFi is busy.

## First power-up checklist

- The gauge appears with dashes in the LCD before WiFi connects.
- Swipe left/right changes the page; a short toast shows the page name.
- Tap shows the IP address; it should match the table above.
- After approving the access request in Signal K, the bezel dot shows blue
  (with a brief blink each second) and the needle moves.
- Touch is read after the CST820's interrupt (GPIO16) and while a finger is
  down; if taps are ignored, check the serial log for touch read errors. If
  the CST820 does not start at all, the unit restarts once and then runs
  without touch (serial log: "Touch did not start").
- If the picture drifts or tears badly while WiFi is busy, lower
  `LVGL_BUFFER_LINES` in `platformio.ini` to free internal RAM.
