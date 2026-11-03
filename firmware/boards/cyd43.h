#pragma once
// Sunton/Guition ESP32-4827S043 (JC4827W543) — the "CYD43".
// ESP32-S3-WROOM-1 N16R8: 16 MB quad flash, 8 MB OCTAL PSRAM.
// 4.3" 480x272 RGB-parallel IPS (ST7262 — a "dumb" panel, no controller: it needs a
// continuous framebuffer streamed from PSRAM). GT911 capacitive touch on I2C0.
//
// WIP: the "grand" combined firmware port (bridge personality on the big S3 screen).
// Pin map is lifted verbatim from the proven gadget port (gadget/port/apply.py
// esp_lcd_rgb_panel_config), which is known-good on this exact hardware.
#define AITL_BOARD_NAME "cyd43"
#define TFT_NATIVE_W 480
#define TFT_NATIVE_H 272
#define AITL_PANEL_RGB 1            // RGB-parallel panel (not SPI) — selects the RGB LGFX class in display.cpp
#define AITL_TOUCH_GT911 1
#define AITL_PANEL_READABLE 0       // RGB panels have no GRAM read-back; screenshots come from the framebuffer
#define AITL_UI_BIG 1              // wide layout

// --- RGB parallel bus (LovyanGFX Bus_RGB). Data pin order = gadget port data_gpio_nums. ---
// B0..B4, G0..G5, R0..R4  (RGB565). If colours look wrong, this order + rgb_order is the first knob.
#define PIN_RGB_D0  8     // B0
#define PIN_RGB_D1  3
#define PIN_RGB_D2  46
#define PIN_RGB_D3  9
#define PIN_RGB_D4  1     // B4
#define PIN_RGB_D5  5     // G0
#define PIN_RGB_D6  6
#define PIN_RGB_D7  7
#define PIN_RGB_D8  15
#define PIN_RGB_D9  16
#define PIN_RGB_D10 4     // G5
#define PIN_RGB_D11 45    // R0
#define PIN_RGB_D12 48
#define PIN_RGB_D13 47
#define PIN_RGB_D14 21
#define PIN_RGB_D15 14    // R4
#define PIN_RGB_DE    40
#define PIN_RGB_VSYNC 41
#define PIN_RGB_HSYNC 39
#define PIN_RGB_PCLK  42
#define RGB_PCLK_HZ   14000000
#define RGB_HSYNC_FP  8
#define RGB_HSYNC_PW  4
#define RGB_HSYNC_BP  43
#define RGB_VSYNC_FP  8
#define RGB_VSYNC_PW  4
#define RGB_VSYNC_BP  12

// --- Backlight: GPIO2, driven high = on (the gadget port leaves it on; never touch GPIO27 here). ---
#define PIN_TFT_BL   2

// --- GT911 capacitive touch on I2C0 ---
#define PIN_TOUCH_SDA 19
#define PIN_TOUCH_SCL 20
#define PIN_TOUCH_RST 38
#define TOUCH_I2C_ADDR 0x5d
