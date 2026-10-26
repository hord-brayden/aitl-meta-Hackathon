#pragma once
// Pins shared by the whole Sunton/"CYD" family (ESP32-WROOM, HSPI display, VSPI microSD, RGB LED, BOOT button)
#define PIN_TFT_SCLK 14
#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12
#define PIN_TFT_CS   15
#define PIN_TFT_DC   2
#define PIN_TFT_RST  -1
#define PIN_TFT_BL   27
#define PIN_SD_SCK   18
#define PIN_SD_MISO  19
#define PIN_SD_MOSI  23
#define PIN_SD_CS    5
#define PIN_LED_R    4
#define PIN_LED_G    16
#define PIN_LED_B    17
#define PIN_BTN_BOOT 0
#define PIN_SPEAKER  26   // small amp/buzzer on the 2432S024/028 boards (applet `beep`)
// I2S microphone on the P3 header (INMP441 / SPH0645): SCK=IO22, WS=IO21, SD=IO35 (input-only). L/R to GND.
#define PIN_MIC_SCK  22
#define PIN_MIC_WS   21
#define PIN_MIC_SD   35
