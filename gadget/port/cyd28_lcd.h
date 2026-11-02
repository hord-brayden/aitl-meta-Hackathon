// ILI9341 LCD bring-up for the LCD Wiki "2.8inch ESP32-S3 Display" (E32C28P):
// a 240x320 ILI9341V panel on SPI, with a plain GPIO backlight. Capacitive touch
// (FT6336, I2C) is handled in musecorder.cpp, not here. Unlike the StickS3 there
// is no PMIC — the panel rail is always powered — so this backend is just the SPI
// panel + an LEDC backlight, mirroring sticks3_lcd.c without the power chip.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Landscape: the case is landscape-only. The native panel is 240x320 portrait,
// rotated to 320x240 via swap_xy in cyd28_lcd.c, so musecorder sees it as landscape.
#define CYD28_LCD_W 320
#define CYD28_LCD_H 240

// Bring up the SPI bus, the ILI9341 panel and the backlight. Paints the panel
// black and sets a default brightness. Safe to call more than once. False on error.
bool cyd28_lcd_init(void);

// True once cyd28_lcd_init() has succeeded.
bool cyd28_lcd_ready(void);

// Blit a w*h block of RGB565 pixels at (x,y). Copies through a DMA bounce buffer
// and waits for the transfer, so the caller's buffer is free on return.
bool cyd28_lcd_draw(int x, int y, int w, int h, const uint16_t *px);

// Backlight level, 0..100 (LEDC PWM on the backlight pin).
void cyd28_lcd_brightness(int pct);

int cyd28_lcd_width(void);
int cyd28_lcd_height(void);

#ifdef __cplusplus
}
#endif
