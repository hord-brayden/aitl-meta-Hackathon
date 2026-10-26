# AGENTS.md — Musecorder

Musecorder is a reconfigurable Muse companion on an ESP32 "Cheap Yellow Display" (CYD). Two halves:

| Path | What | Toolchain |
|---|---|---|
| `firmware/` | ESP32 Arduino + LVGL 8.3 firmware: home/chat UI, bridge link, BLE keyboard, **applet engine** | PlatformIO (`pio`) |
| `bridge/` | Python package `musecorder`: Mac-side bridge (Muse Model API + Muse Code sessions), applet relay, MCP server, CLI | Python 3.10+, venv in `bridge/.venv` |
| `docs/` | `APPLETS.md` (applet spec), `PROTOCOL.md` (link frames), `HARDWARE.md`, `SECURITY.md`, `UI.md` | |
| `examples/applets/` | applet JSON you can install as-is | |
| `.agents/skills/musecorder/` | the skill that teaches an agent to write applets | |

## The two ways to change what the device does

1. **Applet (seconds, no toolchain).** Write a JSON applet per `docs/APPLETS.md`, validate it, push it over the running
   bridge. Use this for counters, timers, loggers, dashboards, pin readers, webhook posters, remote controls.
   ```sh
   musecorder applet validate my.json
   musecorder applet install my.json --open       # then: musecorder applet ui   (read the screen back)
   ```
   From Muse Code with the MCP server registered (`musecorder muse-setup`), call the `applet_install` tool instead.
2. **Native firmware change (minutes, PlatformIO).** When an applet needs something the engine lacks (a new sensor
   driver, a protocol, a new widget or action verb), edit `firmware/src/` and reflash. Add engine features in
   `firmware/src/applets/` (actions in `applet.cpp::runOne`, widgets in `applet_ui.cpp::build`, built-ins in
   `applet.cpp::resolve`) and document them in `docs/APPLETS.md` + the skill.

## Build and flash the firmware

Board: ESP32-2432S024C (classic ESP32-D0WD-V3, 4 MB flash, **no PSRAM**, ILI9341 240×320, CST820 capacitive touch,
CH340 USB serial). Other CYD profiles exist in `firmware/boards/` (`cyd28c`, `cyd28r`, `cyd35r`, `hosyond40r`).

```sh
cd firmware
pio run -e cyd24c                       # build (≈40 s cold, 20 s warm)
pio run -e cyd24c -t upload --upload-port /dev/cu.usbserial-110    # flash (the bridge must not hold the port)
tools/monitor.sh                        # serial log at 460800 baud; device log lines start with "# ", frames are JSON
```

Find the port with `ls /dev/cu.usbserial-* /dev/cu.wchusbserial*`. If the bridge is running (`musecorder serve`),
stop it first; macOS lets only one process own the port. Reflashing keeps NVS settings and the LittleFS applet store;
`tools/erase.sh` wipes everything.

Memory rule of thumb (no PSRAM): ~110 KB heap free with an applet on screen, ~45 KB less with Wi-Fi up. Keep new
allocations small, prefer `JsonDocument` filters when reading files, never block the loop (LVGL runs on it).

## Test without the bridge

`scratch` harness pattern: open the serial port at 460800 with DTR/RTS low, answer the device's `hello` with a
`hello.ack`, then send `applet.*` frames and read the replies (see `docs/APPLETS.md` §6). The bridge CLI does the same
through the running bridge: `musecorder applet ...`.

## Conventions

- Firmware: C++17, Arduino core 2.0.x (PlatformIO `espressif32@6.13.0`), ArduinoJson 7 (`JsonDocument`, elastic),
  LVGL 8.3 with the theme in `src/ui/theme.*`. Files are dense on purpose; keep the style.
- Protocol frames ≤ 6 KB (`LINK_MAX_MSG`); chunk larger payloads (`applet.put`).
- Pins an applet may touch: 0 (BOOT, input), 21, 22, 26 (speaker), 34/35/36/39 (input only). Everything else is the
  display, touch, SD or LED (`firmware/boards/cyd_common.h`).
- Do not commit flash images, keys, tokens or the bridge's `~/.config/musecorder` contents.
- No AI-assistant attribution anywhere in this repository (commits, docs, headers).
