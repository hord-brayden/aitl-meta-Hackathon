# Gadget mode: pair the Musecorder with the Muse app

Meta's open-source [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) turns an ESP32 into a device
your **own Muse** (the one in the phone app) can reach: it pairs over Bluetooth from the app, joins Wi-Fi, and holds an
encrypted session to your Muse. No API key, no per-token cost: it rides on your Muse account.

This folder ports that firmware to the Musecorder's board (Sunton CYD 2.4", classic ESP32, ILI9341). On a board without
PSRAM the SDK gives the "status screen" profile: colour bars for pairing / connecting / connected, the agent's name,
the pixel animation, images Muse sends, and the BOOT button to confirm pairing. The full avatar UI needs PSRAM.

Because the device has one 4 MB flash, gadget mode and Musecorder mode are two images you swap in ~40 s:

```sh
gadget/build.sh                       # once: clones the SDK into gadget/sdk (gitignored), applies gadget/port, builds
gadget/flash.sh /dev/cu.usbserial-110 # -> gadget mode (status bars, pairs with the Muse app)
tools/flash.sh cyd24c                 # -> back to Musecorder (applets, bridge, Muse Code)
```

## Prerequisites (one time)
1. ESP-IDF **v6.0.1** at `~/esp/esp-idf-v6` (`brew install cmake ninja dfu-util`, then
   `git clone -b v6.0.1 --recursive --depth 1 --shallow-submodules https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6 && ~/esp/esp-idf-v6/install.sh esp32`).
2. An SDK token from [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) (Account > SDK tokens). Put it in
   the repo's `.env` as `MUSE_GADGET_SDK_TOKEN=mgst_...` (that file is gitignored). Read the SDK terms there first.
3. The Muse app on your phone with **Settings > Devices > Developer mode** on.

## Pairing
Flash, watch the serial monitor for `link.main: Muse Gadget starting`. The bars breathe orange = ready. In the Muse app
add a device; pick `MuseGadget-CYD-XXXXXX`; pick your Wi-Fi. When the bars breathe blue, press **BOOT** on the board.
Green = connected to your Muse. Hold BOOT 5 s to unpair.

## What the port changes (`gadget/port/apply.py`)
- `devices/sdkconfig.cyd24`: classic ESP32, 4 MB DIO, no PSRAM, BLE-only controller, tunnel off, BOOT on GPIO0,
  BLE name suffix `-CYD`, custom 4 MB partition table (`partitions_cyd24_4mb.csv`: single factory app).
- `main/led_status.c` + `Kconfig.projbuild`: a `HOMEHUB_LED_BACKEND_CYD_ILI9341` status backend (HSPI 14/13, CS 15,
  DC 2, backlight 27, 240×320, BGR, not inverted) next to the existing ST7789 ones; `esp_lcd_ili9341` added to
  `idf_component.yml`.
If the bars show blue where orange is expected, flip `LCD_RGB_ELEMENT_ORDER_BGR` to `RGB` in `apply.py` and rebuild.

## Next step after pairing works
Register device commands (`link.register` in `main/app.c`, see the SDK's `AGENTS.md` "Adding a command") so your
Muse can call `applet.install` straight from the phone, and merge the Musecorder applet engine into this image.
