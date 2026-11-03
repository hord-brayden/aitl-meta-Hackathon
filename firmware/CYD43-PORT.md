# CYD43 "grand" firmware — bridge personality on the ESP32-4827S043

**Status: WIP scaffold. Does not build/run cleanly yet — this is the foundation + plan.**
Goal: bring the full **bridge-mode** firmware (chat UI, Model API, applet engine, HID, dictation) to the
4.3" CYD43 (ESP32-S3, 480×272 RGB panel), so the "grand" build combines bridge's power with the big screen
the gadget firmware runs today. Lives only on the `cyd43-grand` branch — never on `main`.

## Why this is a real port (not a flash)
The bridge firmware (`firmware/`) was written for **classic ESP32 + SPI panels** (`board = esp32dev`,
LovyanGFX `Panel_ST7796/ST7789/ILI9341` over `Bus_SPI`). The CYD43 is an **ESP32-S3 with an RGB-parallel
panel** (ST7262, a dumb panel that streams a framebuffer from PSRAM). Different MCU target, different display
bus. LovyanGFX supports both, so it's bounded work — but it's bring-up, not a recompile.

## What this scaffold already adds
- `boards/cyd43.h` — full pin map (RGB data/control pins, GT911 touch, backlight), copied from the proven
  gadget port (`gadget/port/apply.py`), so the hardware values are known-good.
- `src/config.h` — selects `boards/cyd43.h` on `-DAITL_BOARD_CYD43`, adds the `"rgb"` panel name.
- `src/hal/display.cpp` — a complete `#if defined(AITL_PANEL_RGB)` LGFX class: `Bus_RGB` + `Panel_RGB`
  (framebuffer in PSRAM) + `Touch_GT911`. The SPI path for other boards is untouched (wrapped in `#else`).
- `platformio.ini` — `[env:cyd43]`: S3 target, octal PSRAM (`qio_opi`), 16 MB flash.

## Bring-up checklist (in order)
1. **Compile pass.** `pio run -e cyd43`. Expect LovyanGFX field-name fixups for v1.1.16 (e.g. `config_detail()`,
   `use_psram`, the `Bus_RGB` timing fields). Fix until it links.
2. **Partition table.** Replace the inherited 4 MB `partitions.csv` with a 16 MB table that keeps the LittleFS
   (bridge cert) + applet-store partitions. The app won't fit the 4 MB layout.
3. **Display bring-up.** Flash and confirm the panel lights and draws. Likely knobs: `RGB_PCLK_HZ` (drop if the
   image tears/noises), the D0–D15 bit order and `rgb_order` (if colours are wrong — red/blue swap is the usual
   culprit), and the porch/sync values (already set from the gadget port).
4. **LVGL buffers.** For an RGB panel, render the LVGL draw buffer(s) from PSRAM; revisit `kBufLines` /
   `pushImageDMA` in `display.cpp` — full-framebuffer or large partial buffers work better than the small SPI
   line buffers. Watch heap: PSRAM holds the framebuffer; keep LVGL buffers there too.
5. **Touch.** Confirm GT911 at 0x5d on I²C SDA19/SCL20 (RST38). Capacitive — no calibration wizard needed.
6. **Radios.** S3 has BLE (NimBLE) only, no classic BT — fine for the bridge's BLE HID. Re-check the NimBLE
   build flags in `[env]`.
7. **Then** merge in the gadget niceties you want (the direct Muse pairing path is in `gadget/`, a separate
   ESP-IDF tree — deciding how much to fuse vs keep two images is a design call, step 2 of the goose chase).

## Reference: the known-good panel config (from the gadget port)
480×272, 16-bit RGB565, framebuffer in PSRAM, backlight GPIO2, pclk ~14 MHz, pclk_active_neg.
hsync BP43/FP8/PW4, vsync BP12/FP8/PW4. Control: DE40 VSYNC41 HSYNC39 PCLK42.
Data (B0..B4,G0..G5,R0..R4): 8,3,46,9,1, 5,6,7,15,16,4, 45,48,47,21,14.
Touch GT911: SDA19 SCL20 RST38 addr 0x5d.

## Restore the working gadget firmware anytime
`git checkout cyd43-gadget-stable && BOARD=4827 gadget/build.sh && BOARD=4827 gadget/flash.sh`
or flash `firmware-backup/gadget-4827-cyd43-stable-2026-11-03-*.bin` (see its `-FLASH.md`).
