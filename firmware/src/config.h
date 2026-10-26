#pragma once
// AITL — board profile selection + product constants. Pins live in boards/<board>.h (see boards/README.md).
#ifndef AITL_FW_VERSION
#define AITL_FW_VERSION "0.3.0"
#endif
#define AITL_PRODUCT   "MUSECORDER"
#define AITL_ORG       "CAERULI OBSCURI"
#define AITL_MOTTO     "Ex aperto, claritas."
#define AITL_HOSTNAME  "musecorder"

#if defined(AITL_BOARD_CYD28R)
#include "../boards/cyd28r.h"
#elif defined(AITL_BOARD_CYD24C)
#include "../boards/cyd24c.h"
#elif defined(AITL_BOARD_CYD35R)
#include "../boards/cyd35r.h"
#elif defined(AITL_BOARD_HOSYOND40R)
#include "../boards/hosyond40r.h"
#else
#include "../boards/cyd28c.h"
#endif

#if defined(AITL_PANEL_ST7796)
#define TFT_PANEL_NAME "st7796"
#elif defined(AITL_PANEL_ST7789)
#define TFT_PANEL_NAME "st7789"
#else
#define TFT_PANEL_NAME "ili9341"
#endif
#ifndef TFT_SPI_HZ
#define TFT_SPI_HZ 40000000
#endif
#ifndef AITL_PANEL_READABLE
#define AITL_PANEL_READABLE 1
#endif
#ifndef AITL_DEFAULT_INVERT
#define AITL_DEFAULT_INVERT 0
#endif
#if defined(AITL_TOUCH_XPT2046) || defined(AITL_TOUCH_XPT2046_SHARED)
#define TOUCH_NAME "xpt2046 resistive"
#else
#define TOUCH_NAME "cst8xx capacitive"
#endif

// Link
#define SERIAL_BAUD 460800        // CH340 handles it; must match bridge.serial_baud
#define BRIDGE_WS_PORT_DEFAULT 7777
#define LINK_MAX_MSG 6144         // bytes per NDJSON frame (bridge chunks larger text)
#define CHAT_VIEW_MAX 6000        // bytes of transcript kept on-device per view

// Audio (device microphone)
#define MIC_SAMPLE_RATE 16000
#define MIC_FRAME_SAMPLES 256     // 16 ms per ADPCM frame (128 bytes payload)
