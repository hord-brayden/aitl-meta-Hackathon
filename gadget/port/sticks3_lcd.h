// Self-contained ST7789 LCD driver for the M5Stack StickS3 (1.14" 135x240),
// for the Musecorder port (Link stack, MUSE_ENABLED off). Unlike the CYD/4827
// colour panels, this board's LCD rail is behind the M5PM1 power chip (I2C
// 0x6E), so bring-up powers that rail first, then the SPI panel. led_status.c's
// STICKS3_ST7789 backend drives this; musecorder draws RGB565 rectangles that
// go straight to the panel (no colour conversion, like the other colour panels).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STICKS3_LCD_W 135
#define STICKS3_LCD_H 240

// Power the LCD rail via the PMIC, bring up the SPI ST7789 panel, and turn the
// backlight on. Returns false on any failure (I2C, SPI, or panel).
bool sticks3_lcd_init(void);
// True once sticks3_lcd_init() has succeeded.
bool sticks3_lcd_ready(void);
// Draw w x h RGB565 pixels (native order, as the other colour panels take them)
// at (x, y). Copies into a DMA buffer, then blits. Returns false if not ready
// or the rect is out of range.
bool sticks3_lcd_draw(int x, int y, int w, int h, const uint16_t *px);
// Backlight brightness, 0..100 percent.
void sticks3_lcd_brightness(int pct);
// Switch the PMIC's 5 V output (Grove header + the on-board IR LED) on/off. The
// display bring-up leaves it off; enable it before driving IR. False if the PMIC
// isn't up yet.
bool sticks3_pmic_5v(bool on);
// Read the battery from the M5PM1 PMIC. On success *millivolt is the battery
// voltage in mV and *usb is true when USB / the 5 V input is present (the PM1 has
// no real charge-status register, so USB-present is the charging proxy). Either
// pointer may be NULL. Returns false if the PMIC isn't up yet.
bool sticks3_battery(uint16_t *millivolt, bool *usb);
int sticks3_lcd_width(void);
int sticks3_lcd_height(void);

#ifdef __cplusplus
}
#endif
