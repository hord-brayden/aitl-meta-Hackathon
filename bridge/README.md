# Musecorder bridge

The Mac half of Musecorder: links the ESP32 "CYD" device (USB serial, Wi-Fi WebSocket or Bluetooth) to Meta Muse
(Model API), Muse Code (sessions + tool approvals on the device), macOS automation (typing, dictation, media,
notifications) and the **applet relay** that lets Muse re-program the device with JSON applets on demand.

## Install

```sh
cd bridge
./install.sh                           # = python3 -m venv .venv && . .venv/bin/activate && pip install -e ".[dev,discovery]"
. .venv/bin/activate
musecorder doctor                      # ports, keys, TLS, muse CLI, model catalog
```

`install.sh` also works around iCloud Drive marking `.pth` files hidden (Python then skips them): it symlinks the
package into site-packages so the console scripts import from any directory.

Requires Python 3.10+, the `muse` CLI (`pip install muse-code` / see dev.meta.ai) on PATH or in `~/.local/bin`
for Muse Code features, and `muse login` for a live model.

## Run

```sh
musecorder serve                       # USB; --wifi, --ble, --allow-pairing 180 as needed
musecorder status                      # devices linked to the running bridge
musecorder set brightness 200          # device settings (musecorder set with no args lists keys)
musecorder pair                        # Wi-Fi: serve + pairing window
musecorder provision --ssid NAME --bridge   # push Wi-Fi + bridge credentials over USB
```

Config: `~/.config/musecorder/config.toml` (created on first run). `[providers.muse_code]` sets the `muse` binary,
workspace, permission profile and approval wait; `[applets]` caps `net.http` (timeout 15 s, body 2000 chars,
2 req/s) and `ai.ask` tokens; `[[actions]]` are the one-tap prompts on the device.

## Models and keys

| model id on the device | backed by | needs |
|---|---|---|
| `muse/muse-spark-1.3` (default), `muse/muse-spark-1.2` | Meta Model API `https://api.meta.ai/v1` | `META_API_KEY` or `musecorder keys set muse` (Keychain) |
| `muse-code/default` | Muse Code via `muse serve` + `muse-code-sdk` (falls back to `muse exec --json`) | `muse login` |
| `ollama/*` | local Ollama | `ollama serve` |
| `openai/*`, `openrouter/*`, custom `[[providers.openai_compatible]]` | any OpenAI-style endpoint | their key env |

Keys come from the environment first, then the macOS Keychain. Never paste a key into a chat or a file; if one
leaks, rotate it. Each device chat on `muse-code/*` is its own Muse Code session (resumed by id); tool approvals
show up on the device as Allow / Deny cards. The device's session picker (`cc.list`) lists Muse Code sessions.

Speech-to-text for device-mic dictation (`[stt] engine`): `mlx-whisper` if installed (local), else `muse-cli`
(`muse voice transcribe`), else `muse-api` (Model API `muse-voice-transcribe-1.0`; request body shape still TODO).

## Muse Code integration

```sh
musecorder muse-setup
```

writes `mcpServers.musecorder` (stdio, command `<venv>/bin/musecorder-mcp`) into `~/.config/muse/settings.json`
(backing the file up first, keeping everything else) and copies the skill `.agents/skills/musecorder` to
`~/.agents/skills/musecorder/`. The same skill is picked up as a project skill when Muse Code runs inside this
repo. Restart Muse Code, keep `musecorder serve` running, and ask: "build a door counter on the Musecorder".

Manual registration, if you prefer:

```json
{ "schema_version": 1,
  "mcpServers": { "musecorder": { "transport": "stdio", "command": "/path/to/bridge/.venv/bin/musecorder-mcp", "args": [] } } }
```

MCP tools (`musecorder-mcp`, stdio): `device_status`, `applet_spec`, `applet_validate`, `applet_install(json, open)`,
`applet_list`, `applet_remove`, `applet_open`, `applet_close`, `applet_state_get`, `applet_state_set(id, state_json)`,
`applet_event(id, name)`, `applet_screen`, `applet_log(id)`, `device_type_text`, `device_key`, `device_set`.
Each is a thin wrapper over the loopback admin routes below and returns the device's reply as JSON.

## Applets from the command line

```sh
musecorder applet validate ../examples/applets/*.json      # schema check, no device needed
musecorder applet install ../examples/applets/dice.json --open
musecorder applet list | ui | close
musecorder applet state dice                              # read state
musecorder applet state dice face=6 rolls=0               # merge state (on/off/ints parsed)
musecorder applet event dice roll
musecorder applet log dice                                # device ring + bridge-kept lines
musecorder applet remove dice
```

Documents over ~5 KB are chunked over the link (`applet.put` x N then `applet.install`); every frame stays under 6 KB.

Admin routes on `127.0.0.1:7778` (loopback only): `GET /admin/status|get|probe`, `POST /admin/set|type|key|provision|pair_open`,
`POST /admin/applet/install|remove|open|close|state|event`, `GET /admin/applet/list|ui|state?id=|log?id=`.

## Tests

```sh
pip install -e ".[dev]" && python -m pytest -q
```

## Troubleshooting

- **"bridge not reachable on 127.0.0.1:7778"**: `musecorder serve` is not running (or runs in another user session).
- **"no device linked"**: the device must show USB/Wi-Fi green; check `musecorder doctor` for the serial port
  (CH340 driver) and that nothing else (a serial monitor) holds the port.
- **"device did not answer with applet.ack"**: the firmware rejected or never received the document; run
  `musecorder -v serve` to see frames, and `musecorder applet validate` first.
- **Muse Code model not ready**: `muse login`, then `musecorder models`. For offline testing set
  `providers.muse_code.provider = "echo"` in config.toml (the echo provider answers without credentials).
- **Meta Muse model not ready**: `musecorder keys set muse` or export `META_API_KEY`.
- **Typing / key presses blocked**: grant Accessibility to your terminal (or `~/.config/musecorder/Musecorder Bridge.app`
  when running as the background app) in System Settings > Privacy & Security.
- **Bluetooth**: `musecorder serve --ble` runs as `Musecorder Bridge.app`; `musecorder logs -f`, `musecorder stop`.
- Verbose: `musecorder -v serve`. Auth headers in `net.http` are masked in logs; bodies are capped at 2000 chars.
