#pragma once
#include "cyd_common.h"
#define AITL_BOARD_NAME "cyd28r"
#define TFT_NATIVE_W 240
#define TFT_NATIVE_H 320
#define AITL_PANEL_ILI9341 1
#define AITL_TOUCH_XPT2046 1   // resistive controller on its own pins, read with a slow bit-banged SPI
#define PIN_TOUCH_CLK  25
#define PIN_TOUCH_MOSI 32
#define PIN_TOUCH_MISO 39
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36
