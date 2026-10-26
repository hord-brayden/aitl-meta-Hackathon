#!/usr/bin/env python3
"""Port Meta's muse-gadget-sdk ESP32 firmware to the Sunton CYD 2.4" (ILI9341). Idempotent: run it on a fresh clone
or again after `git pull` inside the SDK. Usage: python3 gadget/port/apply.py gadget/sdk/esp32"""
import pathlib, shutil, sys, re

root = pathlib.Path(sys.argv[1]).resolve()
here = pathlib.Path(__file__).resolve().parent
MARK = "HOMEHUB_LED_BACKEND_CYD_ILI9341"

def edit(path: pathlib.Path, pairs, must=True):
    s = path.read_text(encoding="utf-8"); orig = s
    for old, new in pairs:
        if old not in s:
            if must: sys.exit(f"apply.py: pattern not found in {path.name}: {old[:70]!r} (SDK changed; update gadget/port/apply.py)")
            continue
        s = s.replace(old, new, 1)
    if s != orig:
        path.write_text(s, encoding="utf-8"); print("  patched", path.relative_to(root))

shutil.copy(here / "sdkconfig.cyd24", root / "devices" / "sdkconfig.cyd24")
shutil.copy(here / "sdkconfig.cyd40", root / "devices" / "sdkconfig.cyd40")
shutil.copy(here / "partitions_cyd24_4mb.csv", root / "partitions_cyd24_4mb.csv")
print("  copied devices/sdkconfig.cyd24, devices/sdkconfig.cyd40, partitions_cyd24_4mb.csv")

k = root / "main" / "Kconfig.projbuild"
if MARK not in k.read_text():
    edit(k, [
        ('        config HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n            bool "Waveshare ESP32-C6-LCD-1.47 inch ST7789 display (status bars)"\n            depends on IDF_TARGET_ESP32C6\n',
         '        config HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n            bool "Waveshare ESP32-C6-LCD-1.47 inch ST7789 display (status bars)"\n            depends on IDF_TARGET_ESP32C6\n\n        config HOMEHUB_LED_BACKEND_CYD_ILI9341\n            bool "Sunton CYD 2.4/2.8 inch ILI9341 display (status bars)"\n            depends on IDF_TARGET_ESP32\n\n        config HOMEHUB_LED_BACKEND_CYD_ST7796\n            bool "Hosyond 4.0 inch ST7796 display (status bars)"\n            depends on IDF_TARGET_ESP32\n'),
        ('        default y if HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n', '        default y if HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789\n        default y if HOMEHUB_LED_BACKEND_CYD_ILI9341\n        default y if HOMEHUB_LED_BACKEND_CYD_ST7796\n'),
    ])

c = root / "main" / "led_status.c"
if MARK not in c.read_text():
    s = c.read_text(encoding="utf-8")
    both = "CONFIG_HOMEHUB_LED_BACKEND_IDEASPARK_ST7789 || CONFIG_HOMEHUB_LED_BACKEND_WAVESHARE_C6_ST7789"
    n = s.count(f"#if {both}")
    s = s.replace(f"#if {both}", f"#if {both} || CONFIG_{MARK} || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796")
    s = s.replace('#include "esp_lcd_panel_st7789.h"\n', f'#include "esp_lcd_panel_st7789.h"\n#if CONFIG_{MARK}\n#include "esp_lcd_panel_ili9341.h"\n#endif\n#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n#include "esp_lcd_panel_st7796.h"\n#endif\n', 1)
    block = f'''#elif CONFIG_{MARK}
// Sunton ESP32-2432S024C / 2432S028: 240x320 ILI9341 on HSPI, backlight on GPIO27, reset tied to EN.
#define LCD_NAME         "CYD ILI9341"
#define LCD_HOST         SPI2_HOST
#define LCD_PIN_SCLK     14
#define LCD_PIN_MOSI     13
#define LCD_PIN_CS       15
#define LCD_PIN_DC       2
#define LCD_PIN_RST      -1
#define LCD_PIN_BL       27
#define LCD_PCLK_HZ      (40 * 1000 * 1000)
#define LCD_H_RES        240
#define LCD_V_RES        320
#define LCD_X_GAP        0
#define LCD_BAR_ROWS     12
#define LCD_ANIM_SCALE   4
#define LCD_DOT_MARGIN   6
#define LCD_BUF_CAPS     MALLOC_CAP_DMA
#elif CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796
// Hosyond 4.0" ESP32-32E: 320x480 ST7796 on HSPI, backlight GPIO27, reset tied to EN, panel needs inversion.
#define LCD_NAME         "CYD ST7796"
#define LCD_HOST         SPI2_HOST
#define LCD_PIN_SCLK     14
#define LCD_PIN_MOSI     13
#define LCD_PIN_CS       15
#define LCD_PIN_DC       2
#define LCD_PIN_RST      -1
#define LCD_PIN_BL       27
#define LCD_PCLK_HZ      (40 * 1000 * 1000)
#define LCD_H_RES        320
#define LCD_V_RES        480
#define LCD_X_GAP        0
#define LCD_BAR_ROWS     16
#define LCD_ANIM_SCALE   5
#define LCD_DOT_MARGIN   8
#define LCD_BUF_CAPS     MALLOC_CAP_DMA
#endif

#if CONFIG_HOMEHUB_DISPLAY'''
    old = "#define LCD_BUF_CAPS     MALLOC_CAP_DMA\n#endif\n\n#if CONFIG_HOMEHUB_DISPLAY"
    if old not in s: sys.exit("apply.py: backend block anchor not found in led_status.c")
    s = s.replace(old, "#define LCD_BUF_CAPS     MALLOC_CAP_DMA\n" + block, 1)
    old = "    if (err == ESP_OK) err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);"
    new = f"#if CONFIG_{MARK}\n    if (err == ESP_OK) err = esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel);\n#elif CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n    if (err == ESP_OK) err = esp_lcd_new_panel_st7796(io, &panel_cfg, &s_panel);\n#else\n    if (err == ESP_OK) err = esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel);\n#endif"
    if old not in s: sys.exit("apply.py: panel creation anchor not found")
    s = s.replace(old, new, 1)
    old = "    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_panel, true);"
    new = f"    if (err == ESP_OK) err = esp_lcd_panel_invert_color(s_panel, !CONFIG_{MARK});   // the ILI9341 CYD panel is not inverted"
    if old not in s: sys.exit("apply.py: invert anchor not found")
    s = s.replace(old, new, 1)
    old = "        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,"
    new = f"#if CONFIG_{MARK} || CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796\n        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   // CYD panels take BGR; flip if orange shows as blue\n#else\n        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,\n#endif"
    if old not in s: sys.exit("apply.py: rgb order anchor not found")
    s = s.replace(old, new, 1)
    c.write_text(s, encoding="utf-8"); print(f"  patched main/led_status.c ({n} backend guards widened)")

y = root / "main" / "idf_component.yml"
if "esp_lcd_ili9341" not in y.read_text():
    edit(y, [('  espressif/led_strip: "~3.0"\n', '  espressif/led_strip: "~3.0"\n  espressif/esp_lcd_ili9341: "^1"\n  espressif/esp_lcd_st7796: "^1"\n')])
print("port applied")
