// Minimal SSD1306 128x64 monochrome OLED driver (I2C), self-contained.
// Used by the Musecorder port on boards without a color LCD (Arduino Nano ESP32).
// led_status.c's SSD1306 backend drives this; musecorder draws RGB565 rectangles
// that are thresholded to 1-bit here. Nothing above needs to know it is mono.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_W 128
#define SSD1306_H 64

// Bring up the panel on the given I2C pins (7-bit address, usually 0x3C).
// Returns false if the panel does not ACK.
bool ssd1306_init(int sda_gpio, int scl_gpio, uint8_t addr);
// True once ssd1306_init() has succeeded.
bool ssd1306_ready(void);

void ssd1306_clear(void);                              // framebuffer to all-off (not flushed)
void ssd1306_px(int x, int y, bool on);               // one pixel in the framebuffer
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
// Threshold w x h RGB565 pixels (high byte first, as led_status sends) into the
// framebuffer at (x, y). Pixels brighter than mid-grey turn the dot on.
void ssd1306_blit_rgb565(int x, int y, int w, int h, const uint16_t *px);

void ssd1306_flush(void);                              // push the whole framebuffer over I2C
void ssd1306_flush_rows(int y0, int y1);               // push only the pages covering [y0, y1)
void ssd1306_contrast(uint8_t c);                      // 0..255 brightness

#ifdef __cplusplus
}
#endif
