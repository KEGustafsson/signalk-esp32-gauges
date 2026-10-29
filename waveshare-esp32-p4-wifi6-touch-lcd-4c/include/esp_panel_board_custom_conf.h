/*
 * ESP32_Display_Panel board configuration for the
 * Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (SKU 31522): 4" round 720x720 IPS.
 *
 * ESP32_Display_Panel has no built-in entry for this board, so this custom
 * board file describes it. Every value comes from Waveshare's own sources:
 *   - ESP-IDF BSP waveshare/esp32_p4_wifi6_touch_lcd_xc 3.0.1
 *     (CONFIG_BSP_LCD_TYPE_720_720_4_INCH): JD9365 controller, 2-lane
 *     MIPI-DSI at 1500 Mbps/lane, 80 MHz DPI clock, porches, LCD reset GPIO27,
 *     backlight LEDC on GPIO26 (inverted output), GT911 touch on I2C SDA 7 /
 *     SCL 8, DSI PHY powered from on-chip LDO channel 3 at 2.5 V.
 *   - The vendor init table below is that BSP's 720x720 table, which is
 *     identical to the 4C table in Waveshare's Arduino examples
 *     (ESP32-P4-WIFI6-Touch-LCD-XC, libraries/displays/displays_config.h).
 *
 * Layout follows ESP32_Display_Panel's BOARD_WAVESHARE_ESP32_P4_NANO.h.
 */
#pragma once

#define ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM  (1)

#if ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM

#define ESP_PANEL_BOARD_NAME                "Waveshare:ESP32_P4_WIFI6_TOUCH_LCD_4C"
#define ESP_PANEL_BOARD_WIDTH               (720)
#define ESP_PANEL_BOARD_HEIGHT              (720)

/* LCD */
#define ESP_PANEL_BOARD_USE_LCD             (1)
#define ESP_PANEL_BOARD_LCD_CONTROLLER      JD9365
#define ESP_PANEL_BOARD_LCD_BUS_TYPE        (ESP_PANEL_BUS_TYPE_MIPI_DSI)
    /* For host */
    #define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_NUM           (2)
    #define ESP_PANEL_BOARD_LCD_MIPI_DSI_LANE_RATE_MBPS     (1500)
    /* For refresh panel (DPI) */
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_CLK_MHZ            (80)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_PIXEL_BITS         (ESP_PANEL_LCD_COLOR_BITS_RGB565)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HPW                (20)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HBP                (20)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_HFP                (40)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VPW                (4)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VBP                (12)
    #define ESP_PANEL_BOARD_LCD_MIPI_DPI_VFP                (24)
    /* For DSI power PHY */
    #define ESP_PANEL_BOARD_LCD_MIPI_PHY_LDO_ID             (3)

#define ESP_PANEL_BOARD_LCD_VENDOR_INIT_CMD() \
    { \
        {0xE0, (uint8_t []){0x00}, 1, 0}, \
        {0xE1, (uint8_t []){0x93}, 1, 0}, \
        {0xE2, (uint8_t []){0x65}, 1, 0}, \
        {0xE3, (uint8_t []){0xF8}, 1, 0}, \
        {0x80, (uint8_t []){0x01}, 1, 0}, \
        {0xE0, (uint8_t []){0x01}, 1, 0}, \
        {0x00, (uint8_t []){0x00}, 1, 0}, \
        {0x01, (uint8_t []){0x41}, 1, 0}, \
        {0x03, (uint8_t []){0x10}, 1, 0}, \
        {0x04, (uint8_t []){0x44}, 1, 0}, \
        {0x17, (uint8_t []){0x00}, 1, 0}, \
        {0x18, (uint8_t []){0xD0}, 1, 0}, \
        {0x19, (uint8_t []){0x00}, 1, 0}, \
        {0x1A, (uint8_t []){0x00}, 1, 0}, \
        {0x1B, (uint8_t []){0xD0}, 1, 0}, \
        {0x1C, (uint8_t []){0x00}, 1, 0}, \
        {0x24, (uint8_t []){0xFE}, 1, 0}, \
        {0x35, (uint8_t []){0x26}, 1, 0}, \
        {0x37, (uint8_t []){0x09}, 1, 0}, \
        {0x38, (uint8_t []){0x04}, 1, 0}, \
        {0x39, (uint8_t []){0x08}, 1, 0}, \
        {0x3A, (uint8_t []){0x0A}, 1, 0}, \
        {0x3C, (uint8_t []){0x78}, 1, 0}, \
        {0x3D, (uint8_t []){0xFF}, 1, 0}, \
        {0x3E, (uint8_t []){0xFF}, 1, 0}, \
        {0x3F, (uint8_t []){0xFF}, 1, 0}, \
        {0x40, (uint8_t []){0x04}, 1, 0}, \
        {0x41, (uint8_t []){0x64}, 1, 0}, \
        {0x42, (uint8_t []){0xC7}, 1, 0}, \
        {0x43, (uint8_t []){0x18}, 1, 0}, \
        {0x44, (uint8_t []){0x0B}, 1, 0}, \
        {0x45, (uint8_t []){0x14}, 1, 0}, \
        {0x55, (uint8_t []){0x02}, 1, 0}, \
        {0x57, (uint8_t []){0x49}, 1, 0}, \
        {0x59, (uint8_t []){0x0A}, 1, 0}, \
        {0x5A, (uint8_t []){0x1B}, 1, 0}, \
        {0x5B, (uint8_t []){0x19}, 1, 0}, \
        {0x5D, (uint8_t []){0x7F}, 1, 0}, \
        {0x5E, (uint8_t []){0x56}, 1, 0}, \
        {0x5F, (uint8_t []){0x43}, 1, 0}, \
        {0x60, (uint8_t []){0x37}, 1, 0}, \
        {0x61, (uint8_t []){0x33}, 1, 0}, \
        {0x62, (uint8_t []){0x25}, 1, 0}, \
        {0x63, (uint8_t []){0x2A}, 1, 0}, \
        {0x64, (uint8_t []){0x16}, 1, 0}, \
        {0x65, (uint8_t []){0x30}, 1, 0}, \
        {0x66, (uint8_t []){0x2F}, 1, 0}, \
        {0x67, (uint8_t []){0x32}, 1, 0}, \
        {0x68, (uint8_t []){0x53}, 1, 0}, \
        {0x69, (uint8_t []){0x43}, 1, 0}, \
        {0x6A, (uint8_t []){0x4C}, 1, 0}, \
        {0x6B, (uint8_t []){0x40}, 1, 0}, \
        {0x6C, (uint8_t []){0x3D}, 1, 0}, \
        {0x6D, (uint8_t []){0x31}, 1, 0}, \
        {0x6E, (uint8_t []){0x20}, 1, 0}, \
        {0x6F, (uint8_t []){0x0F}, 1, 0}, \
        {0x70, (uint8_t []){0x7F}, 1, 0}, \
        {0x71, (uint8_t []){0x56}, 1, 0}, \
        {0x72, (uint8_t []){0x43}, 1, 0}, \
        {0x73, (uint8_t []){0x37}, 1, 0}, \
        {0x74, (uint8_t []){0x33}, 1, 0}, \
        {0x75, (uint8_t []){0x25}, 1, 0}, \
        {0x76, (uint8_t []){0x2A}, 1, 0}, \
        {0x77, (uint8_t []){0x16}, 1, 0}, \
        {0x78, (uint8_t []){0x30}, 1, 0}, \
        {0x79, (uint8_t []){0x2F}, 1, 0}, \
        {0x7A, (uint8_t []){0x32}, 1, 0}, \
        {0x7B, (uint8_t []){0x53}, 1, 0}, \
        {0x7C, (uint8_t []){0x43}, 1, 0}, \
        {0x7D, (uint8_t []){0x4C}, 1, 0}, \
        {0x7E, (uint8_t []){0x40}, 1, 0}, \
        {0x7F, (uint8_t []){0x3D}, 1, 0}, \
        {0x80, (uint8_t []){0x31}, 1, 0}, \
        {0x81, (uint8_t []){0x20}, 1, 0}, \
        {0x82, (uint8_t []){0x0F}, 1, 0}, \
        {0xE0, (uint8_t []){0x02}, 1, 0}, \
        {0x00, (uint8_t []){0x5F}, 1, 0}, \
        {0x01, (uint8_t []){0x5F}, 1, 0}, \
        {0x02, (uint8_t []){0x5E}, 1, 0}, \
        {0x03, (uint8_t []){0x5E}, 1, 0}, \
        {0x04, (uint8_t []){0x50}, 1, 0}, \
        {0x05, (uint8_t []){0x48}, 1, 0}, \
        {0x06, (uint8_t []){0x48}, 1, 0}, \
        {0x07, (uint8_t []){0x4A}, 1, 0}, \
        {0x08, (uint8_t []){0x4A}, 1, 0}, \
        {0x09, (uint8_t []){0x44}, 1, 0}, \
        {0x0A, (uint8_t []){0x44}, 1, 0}, \
        {0x0B, (uint8_t []){0x46}, 1, 0}, \
        {0x0C, (uint8_t []){0x46}, 1, 0}, \
        {0x0D, (uint8_t []){0x5F}, 1, 0}, \
        {0x0E, (uint8_t []){0x5F}, 1, 0}, \
        {0x0F, (uint8_t []){0x57}, 1, 0}, \
        {0x10, (uint8_t []){0x57}, 1, 0}, \
        {0x11, (uint8_t []){0x77}, 1, 0}, \
        {0x12, (uint8_t []){0x77}, 1, 0}, \
        {0x13, (uint8_t []){0x40}, 1, 0}, \
        {0x14, (uint8_t []){0x42}, 1, 0}, \
        {0x15, (uint8_t []){0x5F}, 1, 0}, \
        {0x16, (uint8_t []){0x5F}, 1, 0}, \
        {0x17, (uint8_t []){0x5F}, 1, 0}, \
        {0x18, (uint8_t []){0x5E}, 1, 0}, \
        {0x19, (uint8_t []){0x5E}, 1, 0}, \
        {0x1A, (uint8_t []){0x50}, 1, 0}, \
        {0x1B, (uint8_t []){0x49}, 1, 0}, \
        {0x1C, (uint8_t []){0x49}, 1, 0}, \
        {0x1D, (uint8_t []){0x4B}, 1, 0}, \
        {0x1E, (uint8_t []){0x4B}, 1, 0}, \
        {0x1F, (uint8_t []){0x45}, 1, 0}, \
        {0x20, (uint8_t []){0x45}, 1, 0}, \
        {0x21, (uint8_t []){0x47}, 1, 0}, \
        {0x22, (uint8_t []){0x47}, 1, 0}, \
        {0x23, (uint8_t []){0x5F}, 1, 0}, \
        {0x24, (uint8_t []){0x5F}, 1, 0}, \
        {0x25, (uint8_t []){0x57}, 1, 0}, \
        {0x26, (uint8_t []){0x57}, 1, 0}, \
        {0x27, (uint8_t []){0x77}, 1, 0}, \
        {0x28, (uint8_t []){0x77}, 1, 0}, \
        {0x29, (uint8_t []){0x41}, 1, 0}, \
        {0x2A, (uint8_t []){0x43}, 1, 0}, \
        {0x2B, (uint8_t []){0x5F}, 1, 0}, \
        {0x2C, (uint8_t []){0x1E}, 1, 0}, \
        {0x2D, (uint8_t []){0x1E}, 1, 0}, \
        {0x2E, (uint8_t []){0x1F}, 1, 0}, \
        {0x2F, (uint8_t []){0x1F}, 1, 0}, \
        {0x30, (uint8_t []){0x10}, 1, 0}, \
        {0x31, (uint8_t []){0x07}, 1, 0}, \
        {0x32, (uint8_t []){0x07}, 1, 0}, \
        {0x33, (uint8_t []){0x05}, 1, 0}, \
        {0x34, (uint8_t []){0x05}, 1, 0}, \
        {0x35, (uint8_t []){0x0B}, 1, 0}, \
        {0x36, (uint8_t []){0x0B}, 1, 0}, \
        {0x37, (uint8_t []){0x09}, 1, 0}, \
        {0x38, (uint8_t []){0x09}, 1, 0}, \
        {0x39, (uint8_t []){0x1F}, 1, 0}, \
        {0x3A, (uint8_t []){0x1F}, 1, 0}, \
        {0x3B, (uint8_t []){0x17}, 1, 0}, \
        {0x3C, (uint8_t []){0x17}, 1, 0}, \
        {0x3D, (uint8_t []){0x17}, 1, 0}, \
        {0x3E, (uint8_t []){0x17}, 1, 0}, \
        {0x3F, (uint8_t []){0x03}, 1, 0}, \
        {0x40, (uint8_t []){0x01}, 1, 0}, \
        {0x41, (uint8_t []){0x1F}, 1, 0}, \
        {0x42, (uint8_t []){0x1E}, 1, 0}, \
        {0x43, (uint8_t []){0x1E}, 1, 0}, \
        {0x44, (uint8_t []){0x1F}, 1, 0}, \
        {0x45, (uint8_t []){0x1F}, 1, 0}, \
        {0x46, (uint8_t []){0x10}, 1, 0}, \
        {0x47, (uint8_t []){0x06}, 1, 0}, \
        {0x48, (uint8_t []){0x06}, 1, 0}, \
        {0x49, (uint8_t []){0x04}, 1, 0}, \
        {0x4A, (uint8_t []){0x04}, 1, 0}, \
        {0x4B, (uint8_t []){0x0A}, 1, 0}, \
        {0x4C, (uint8_t []){0x0A}, 1, 0}, \
        {0x4D, (uint8_t []){0x08}, 1, 0}, \
        {0x4E, (uint8_t []){0x08}, 1, 0}, \
        {0x4F, (uint8_t []){0x1F}, 1, 0}, \
        {0x50, (uint8_t []){0x1F}, 1, 0}, \
        {0x51, (uint8_t []){0x17}, 1, 0}, \
        {0x52, (uint8_t []){0x17}, 1, 0}, \
        {0x53, (uint8_t []){0x17}, 1, 0}, \
        {0x54, (uint8_t []){0x17}, 1, 0}, \
        {0x55, (uint8_t []){0x02}, 1, 0}, \
        {0x56, (uint8_t []){0x00}, 1, 0}, \
        {0x57, (uint8_t []){0x1F}, 1, 0}, \
        {0xE0, (uint8_t []){0x02}, 1, 0}, \
        {0x58, (uint8_t []){0x40}, 1, 0}, \
        {0x59, (uint8_t []){0x00}, 1, 0}, \
        {0x5A, (uint8_t []){0x00}, 1, 0}, \
        {0x5B, (uint8_t []){0x30}, 1, 0}, \
        {0x5C, (uint8_t []){0x01}, 1, 0}, \
        {0x5D, (uint8_t []){0x30}, 1, 0}, \
        {0x5E, (uint8_t []){0x01}, 1, 0}, \
        {0x5F, (uint8_t []){0x02}, 1, 0}, \
        {0x60, (uint8_t []){0x30}, 1, 0}, \
        {0x61, (uint8_t []){0x03}, 1, 0}, \
        {0x62, (uint8_t []){0x04}, 1, 0}, \
        {0x63, (uint8_t []){0x04}, 1, 0}, \
        {0x64, (uint8_t []){0xA6}, 1, 0}, \
        {0x65, (uint8_t []){0x43}, 1, 0}, \
        {0x66, (uint8_t []){0x30}, 1, 0}, \
        {0x67, (uint8_t []){0x73}, 1, 0}, \
        {0x68, (uint8_t []){0x05}, 1, 0}, \
        {0x69, (uint8_t []){0x04}, 1, 0}, \
        {0x6A, (uint8_t []){0x7F}, 1, 0}, \
        {0x6B, (uint8_t []){0x08}, 1, 0}, \
        {0x6C, (uint8_t []){0x00}, 1, 0}, \
        {0x6D, (uint8_t []){0x04}, 1, 0}, \
        {0x6E, (uint8_t []){0x04}, 1, 0}, \
        {0x6F, (uint8_t []){0x88}, 1, 0}, \
        {0x75, (uint8_t []){0xD9}, 1, 0}, \
        {0x76, (uint8_t []){0x00}, 1, 0}, \
        {0x77, (uint8_t []){0x33}, 1, 0}, \
        {0x78, (uint8_t []){0x43}, 1, 0}, \
        {0xE0, (uint8_t []){0x00}, 1, 0}, \
        {0x11, (uint8_t []){0x00}, 1, 120}, \
        {0x29, (uint8_t []){0x00}, 1, 20}, \
        {0x35, (uint8_t []){0x00}, 1, 0}, \
    }

#define ESP_PANEL_BOARD_LCD_COLOR_BITS          (ESP_PANEL_LCD_COLOR_BITS_RGB565)
#define ESP_PANEL_BOARD_LCD_COLOR_BGR_ORDER     (0)     // 0: RGB, 1: BGR
#define ESP_PANEL_BOARD_LCD_COLOR_INEVRT_BIT    (0)
#define ESP_PANEL_BOARD_LCD_SWAP_XY             (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_X            (0)
#define ESP_PANEL_BOARD_LCD_MIRROR_Y            (0)
#define ESP_PANEL_BOARD_LCD_GAP_X               (0)
#define ESP_PANEL_BOARD_LCD_GAP_Y               (0)
#define ESP_PANEL_BOARD_LCD_RST_IO              (27)
#define ESP_PANEL_BOARD_LCD_RST_LEVEL           (0)

/* Touch: GT911-compatible (GT9271 fitted), reset and interrupt not routed */
#define ESP_PANEL_BOARD_USE_TOUCH               (1)
#define ESP_PANEL_BOARD_TOUCH_CONTROLLER        GT911
#define ESP_PANEL_BOARD_TOUCH_BUS_TYPE          (ESP_PANEL_BUS_TYPE_I2C)
#define ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (0)
    #define ESP_PANEL_BOARD_TOUCH_I2C_HOST_ID           (0)
    #define ESP_PANEL_BOARD_TOUCH_I2C_CLK_HZ            (400 * 1000)
    #define ESP_PANEL_BOARD_TOUCH_I2C_SCL_PULLUP        (1)
    #define ESP_PANEL_BOARD_TOUCH_I2C_SDA_PULLUP        (1)
    #define ESP_PANEL_BOARD_TOUCH_I2C_IO_SCL            (8)
    #define ESP_PANEL_BOARD_TOUCH_I2C_IO_SDA            (7)
    /* 0x5D. Some boards answer at the GT911 backup address 0x14 instead;
     * common/src/board_start.cpp tries it after a failed begin(). */
    #define ESP_PANEL_BOARD_TOUCH_I2C_ADDRESS           (0)
#define ESP_PANEL_BOARD_TOUCH_SWAP_XY           (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_X          (0)
#define ESP_PANEL_BOARD_TOUCH_MIRROR_Y          (0)
#define ESP_PANEL_BOARD_TOUCH_RST_IO            (-1)
#define ESP_PANEL_BOARD_TOUCH_RST_LEVEL         (0)
#define ESP_PANEL_BOARD_TOUCH_INT_IO            (-1)
#define ESP_PANEL_BOARD_TOUCH_INT_LEVEL         (0)

/* Backlight: LEDC PWM; the BSP drives it with output_invert = 1, i.e. the
 * backlight is on when the pin is low. */
#define ESP_PANEL_BOARD_USE_BACKLIGHT           (1)
#define ESP_PANEL_BOARD_BACKLIGHT_TYPE          (ESP_PANEL_BACKLIGHT_TYPE_PWM_LEDC)
    #define ESP_PANEL_BOARD_BACKLIGHT_IO        (26)
    #define ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL  (0)
#define ESP_PANEL_BOARD_BACKLIGHT_IDLE_OFF      (0)

#define ESP_PANEL_BOARD_USE_EXPANDER            (0)

#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MAJOR 1
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_MINOR 2
#define ESP_PANEL_BOARD_CUSTOM_FILE_VERSION_PATCH 0

#endif // ESP_PANEL_BOARD_DEFAULT_USE_CUSTOM
