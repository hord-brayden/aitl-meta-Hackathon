# Hardware notes — 2.8" capacitive CYD

Probe sketch: `tools/probe/` (`pio run -t upload`, then open the serial monitor). Output on this unit (2026-10-20):

```
Chip: ESP32-D0WD-V3 (rev 3.1), 4MB flash (DIO), 40 MHz crystal, no PSRAM
I2C scan SDA=33 SCL=32: 0x15
  CST8xx @0x15: chipID=0xB7 proj=0x84 fw=0x01   -> CST820
I2C scan SDA=21 SCL=22: (none)
Display ID reads: RDDID 00 00 00 00, RDID4 00 00 00 00, RDDPM 98, RDDCOLMOD 06
  (zeros on 0xD3 rule out ILI9341; panel answers on MISO -> ST7789 class, the JC2432W328C layout)
Backlight(27) + blue LED(17) blinked; SD: no card inserted
```

## Pin map (`firmware/src/config.h`)

| Function | GPIO |
|---|---|
| TFT SCLK / MOSI / MISO | 14 / 13 / 12 (HSPI) |
| TFT CS / DC / RST | 15 / 2 / – |
| Backlight (PWM) | 27 |
| Touch SDA / SCL / RST / INT | 33 / 32 / 25 / 21 |
| microSD SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 (VSPI) |
| RGB LED R / G / B (active low) | 4 / 16 / 17 |
| BOOT button (push-to-talk) | 0 |
| Spare on P3 header | 22, 35 (input-only), 21 shared with touch INT |

## Hosyond 4.0" ESP32-32E (`hosyond40r`) — probed 2026-10-20
ESP32-D0WD-V3, 4 MB, no PSRAM. ST7796 320×480 on HSPI (readable=false, inverted panel), XPT2046 resistive touch
sharing the LCD bus (CS 33, IRQ 36, 1.6 MHz), microSD on VSPI (CS 5), RGB LED R=22 G=16 B=17, speaker amp enable IO4
(held low), speaker IO26. Probe: no I²C touch, XPT2046 idle reads 0/0/0 with IRQ high, SD card detected (32 GB).
Mic pins in the profile (25/21/35) are unverified guesses for this board.

## Microphone (I²S, P3 header)
| INMP441 pin | CYD P3 |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCK | IO22 |
| WS | IO21 (shared with the unused touch INT; touch is polled) |
| SD | IO35 (input-only, ideal) |
| L/R | GND (left channel) |
Capture: 16 kHz mono, 24-bit → 16-bit with `micGain` shift, IMA ADPCM 4:1 (8 KB/s), 128-byte frames as base64 in
`audio.chunk` NDJSON frames. Silence detector stops after ~1.6 s of quiet once speech was heard; 8 s of nothing cancels.

## Board profiles
See `firmware/boards/README.md`. Resistive boards use a bit-banged XPT2046 reader on IO25/32/39/33 because both SPI
hosts are taken (display on HSPI, microSD on VSPI). The calibration wizard solves scale as well as axis flips.

## Memory budget (classic ESP32, no PSRAM)
~320 KB RAM total; after LVGL + draw buffers ~85 KB free. Bluetooth (NimBLE) ~45 KB, Wi-Fi stack ~40 KB, a TLS session
~50 KB. Hence: Wi-Fi and Bluetooth are mutually exclusive, Wi-Fi mode uses a single draw buffer, toggling Wi-Fi restarts
the device (the radio stacks don't return all memory), and the Wi-Fi link signs frames instead of TLS by default.

## Things to know
- **USB-C is a CH340 serial bridge, not native USB.** The classic ESP32 cannot be a USB HID device; "wired HID" is
  implemented by the bridge (System Events on the Mac). Bluetooth HID is native and works with any host.
- **macOS resets the board whenever a program opens the serial port** (the driver asserts DTR). The firmware persists
  the last chat and resumes it after the ~2 s reboot; the bridge waits for the device's `hello` before talking.
- **No PSRAM.** Heap after display+LVGL ≈ 160 KB. Wi-Fi TLS costs ~45 KB during the handshake, BLE ~40 KB. Enable
  BLE and Wi-Fi together only if you need both; the UI stays responsive either way.
- The capacitive controller has no pressure output. "Touch sensitivity" is a debounce (consecutive samples required
  before a press registers); long-press time is separate. Calibration solves axis swap / inversion / offset.
- Panel color flags (invert, R/B order) and rotation are runtime settings so one binary covers panel variants.
- Serial runs at 460800 baud with a 16 KB receive buffer; large bridge replies are paced so LVGL redraws never drop frames.
- Partition table: 3 MB app, 0.9 MB LittleFS (bridge certificate), coredump. No OTA slot by design (room for later).
