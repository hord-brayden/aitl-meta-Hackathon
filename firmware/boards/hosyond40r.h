#pragma once
// Hosyond 4.0" ESP32-32E display (LCDWIKI E32R40T): ESP32-WROOM-32E, 4MB, no PSRAM.
// ST7796 320x480 on HSPI; XPT2046 resistive touch SHARES the LCD SPI bus (CS 33, IRQ 36); microSD on VSPI;
// RGB LED R=22 G=16 B=17 (active low); audio amp enable IO4, speaker IO26. Verified config from TouchMarauder4.
#define AITL_BOARD_NAME "hosyond40r"
#define TFT_NATIVE_W 320
#define TFT_NATIVE_H 480
#define AITL_PANEL_ST7796 1
#define AITL_PANEL_READABLE 0
#define AITL_DEFAULT_INVERT 1          // these ST7796 panels show a negative image without inversion
#define AITL_TOUCH_XPT2046_SHARED 1    // LovyanGFX Touch_XPT2046 on the LCD bus
#define AITL_UI_BIG 1                  // 480-wide layout: larger fonts, taller buttons
#define PIN_TFT_SCLK 14
#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12
#define PIN_TFT_CS   15
#define PIN_TFT_DC   2
#define PIN_TFT_RST  -1
#define PIN_TFT_BL   27
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36
#define TOUCH_SPI_HZ   1600000
#define TFT_SPI_HZ     80000000   // ESP32 SPI max; the panel tolerates it on short traces. Drop to 40000000 if the image shows noise.
#define PIN_SD_SCK   18
#define PIN_SD_MISO  19
#define PIN_SD_MOSI  23
#define PIN_SD_CS    5
#define PIN_LED_R    22
#define PIN_LED_G    16
#define PIN_LED_B    17
#define PIN_BTN_BOOT 0
#define PIN_AUDIO_EN 4                 // keep LOW: amp off
// I2S mic header on this board is not standard; these are free-looking pins, verify before soldering.
#define PIN_MIC_SCK  25
#define PIN_MIC_WS   21
#define PIN_MIC_SD   35
