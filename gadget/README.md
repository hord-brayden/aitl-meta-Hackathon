# Gadget mode: pair the Musecorder with the Muse app

Meta's open-source [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) turns an ESP32 into a device
your **own Muse** (the one in the phone app) can reach: it pairs over Bluetooth from the app, joins Wi-Fi, and holds an
encrypted session to your Muse. No API key, no per-token cost: it rides on your Muse account.

This folder ports that firmware to the Musecorder's board (Sunton CYD 2.4", classic ESP32, ILI9341). On a board without
PSRAM the SDK gives the "status screen" profile: colour bars for pairing / connecting / connected, the agent's name,
the pixel animation, images Muse sends, and the BOOT button to confirm pairing. The full avatar UI needs PSRAM.

Because the device has one 4 MB flash, gadget mode and Musecorder mode are two images you swap in ~40 s:

```sh
gadget/build.sh                       # once: clones the SDK into ~/esp/muse-gadget-sdk (no spaces in the path!), applies gadget/port, builds
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

## Several Wi-Fi networks (home, office, the venue)

Meta's firmware remembers up to eight networks and joins whichever is nearby, but the app only teaches it one, during
setup. The port adds two ways to add more without re-pairing:

- **From the phone, once online:** tell your Muse "remember my office Wi-Fi", it calls `wifi.add` (`wifi.list`,
  `wifi.forget` exist too). The password goes over the encrypted session and is stored on the device only.
- **Over USB, when the device is somewhere it knows no network:** put `GADGET_WIFI_SSID=` and `GADGET_WIFI_PASS=`
  in the repo's gitignored `.env`, run `gadget/wifi.py`; the board stores the network, restarts and joins.
  `gadget/wifi.py --list` shows the names it knows.

A 5 s BOOT hold still erases everything and re-enters pairing, even while an applet runs.

## microSD card

The CYD boards' card slot (VSPI, CS 5) becomes storage your Muse can use, mounted only while an action runs because a
mounted FAT volume costs ~8 KB of this board's RAM:

| Muse command (one `sd` command with an `op`, so the registration stays small) / applet action `sd.*` | what it does |
|---|---|
| `sd {op:append,file,text}` | append a line: logs that need no network and survive reboots |
| `sd {op:read,file,tail}` | the last bytes of a file (into state with `to` as an action) |
| `sd {op:list}`, `sd {op:delete,file}` | the `mc` folder |
| `sd {op:fetch,path,file}` | copy a file from the Muse VM onto the card in 2 KB Range chunks over the control session |
| `sd {op:draw,file,row}` | show a baseline JPEG from the card through Meta's decoder: no download, so no memory wall |

"Fetch workspace/cat.jpg to the card, then draw it" is the image cache: one download, shown offline afterwards.
`gadget/port/apply.py` teaches Meta's `image_fetch.c` a file source for that (`image_fetch_start_file`).

## What the port changes (`gadget/port/apply.py`)
- `devices/sdkconfig.cyd24`: classic ESP32, 4 MB DIO, no PSRAM, BLE-only controller, tunnel off, BOOT on GPIO0,
  BLE name suffix `-CYD`, custom 4 MB partition table (`partitions_cyd24_4mb.csv`: single factory app).
- `main/led_status.c` + `Kconfig.projbuild`: a `HOMEHUB_LED_BACKEND_CYD_ILI9341` status backend (HSPI 14/13, CS 15,
  DC 2, backlight 27, 240×320, BGR, not inverted) next to the existing ST7789 ones; `esp_lcd_ili9341` added to
  `idf_component.yml`.
If the bars show blue where orange is expected, flip `LCD_RGB_ELEMENT_ORDER_BGR` to `RGB` in `apply.py` and rebuild.

## Applets over Meta's channel (phone only, no Mac)

The port builds a Musecorder applet runtime into Meta's firmware (`gadget/port/musecorder/`, `CONFIG_HOMEHUB_MUSECORDER`).
Once the gadget is paired and online, your Muse sees four extra commands; the full applet format rides in the
description of `applet.install` (`musecorder.h`), so Muse needs no other docs:

| command | what it does |
|---|---|
| `applet.install` (json \| id) | store and start a JSON tool (under 4 KB, five stored); `id` alone restarts a stored one |
| `applet.remove` (id? all?) | stop the running applet (it stays stored); `id` deletes one; `all` wipes the store |
| `applet.state` (set? calibrate?) | state, log, stored ids, free RAM; assign keys first; `calibrate` runs the touch setup |
| `applet.event` (name) | fire a custom event |

An applet: `screen` rows (`text`, `big`, `bar{max}`, `gap`, `button{event,color}`, `buttons[{label,event}]`,
`canvas`), `on` handlers (`start`, `stop`, `boot:press`, `touch`, `touch:move`, `every:SECONDS`,
`gpio:PIN:rise|fall|change`, custom names) and actions: `set inc dec toggle log if exit`, `led`, `tone` (speaker,
one note or a sequence), `pwm`, `gpio.read/write`, `adc.read`, `i2c.scan/read/write` (bus pins from the applet's
`i2c{sda,scl}`; `cmd:[..]` sends a multi-byte command and `wait` pauses in ms, enough for Sensirion sensors such as the SCD41), `draw.rect`, `draw.text`, `clear`, `http`, `muse.say`, `muse.ask`, `vm.write`. `${expr}` templates
evaluate against `state` (math, comparisons, `len int round pad clock hhmmss random uptime`). Examples to paste
into the chat: [`../examples/gadget/`](../examples/gadget/).

**Touch.** The 4" board's XPT2046 sits on the LCD's SPI bus (CS 33, pen IRQ 36); the 2.4" board's CST820 is on
I²C (33/32). Tap the applet's title (or the idle status screen) for the **launcher**: the stored applets, "Status
screen", "Touch setup" (three taps calibrate axis order, direction and scale; stored in the applet partition).

**Storage.** Applets, their persisted state and the touch calibration live in their own NVS partition `applets`
(`partitions_cyd24_4mb.csv`, 128 KB at the end of flash), never in Meta's `nvs` with the pairing data. Flashing a
build with this table over the old one keeps the pairing; the runtime moves an applet from the old location on
first boot.

**A bad applet cannot take the device down.** The applet runs in its own task; an applet that crashes the
firmware is left stored but not relaunched after the reset, and the serial console has `>applet=stop`
(clear the autostart), `>applet=list` and `>applet=dump[:id]` (print the stored JSON). The classic ESP32 has no
PSRAM, so a large applet can leave too little RAM for the Muse control session (about 62 KB of buffers next
to TLS): when that happens the runtime pauses the applet, lets the link register, then relaunches it with its
saved state. The link to your Muse always wins, so the device never goes dark from the phone.

**Talking back.** `muse.say` posts a message into your chat with your Muse (the chat route Meta's own boards use), so
"door count hit 50" shows up in the app and Muse can act on it. `muse.ask` additionally reads the reply back onto
the screen on boards with PSRAM; on the classic ESP32 the 16 KB reply frames do not fit, so it posts only.

**Memory on the classic ESP32.** Meta's session plus TLS leaves ~60 KB free with an applet running, so the runtime
keeps every buffer small; a second TLS connection (`http` to https://, `display.draw_url` with https://) fails for
lack of a 17 KB contiguous block. Plain HTTP works: `display.draw_url` with an `http://` image on your LAN (a 160 px
JPEG drew in 320 ms). Boards with PSRAM (Freenove FNK0104B, Guition JC3248W535, Waveshare LCD-7) have none of these
limits.

**Credential scope.** The VM answered `403 route not allowed for this credential's scopes` to `POST /fs/write` and
`GET /identity` for this community-paired device (nothing in the firmware or the SDK token sets scopes), so
`vm.write` is blocked for now; Muse pulls results with `applet.state` and the device pushes with `muse.say`.

**Your Muse as the status sprite.** `gadget/port/gen_avatar_anim.py` turns a GIF into Meta's cell-sprite format;
`apply.py` installs the result.
