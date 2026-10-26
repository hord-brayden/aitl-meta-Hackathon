# Security model

**Goal:** the device never holds a provider API key, nothing on it can be replayed from the network, and every
action that touches your Mac is attributable to a paired device.

## Where secrets live
| Secret | Location | Notes |
|---|---|---|
| Provider API keys | macOS Keychain (`aitl keys set …`) or env vars | never written to `config.toml`, never sent to the device |
| Bridge TLS private key | `~/.config/aitl/tls/bridge.key` (0600) | self-signed P-256, 10 years |
| Device tokens | device NVS (plaintext) · bridge stores **SHA-256 only** (`devices.json`) | revoke with `aitl devices revoke <id>` |
| Wi-Fi password | device NVS | pushed over USB by `aitl provision`, or typed on-device |
| Chats | `~/.config/aitl/chats/*.json` (0600) · optional copy on microSD | the SD copy is plaintext; disable in Setup › Storage |

## Transports
- **USB serial** — physical possession is the credential. The bridge treats the serial device as trusted, exactly
  like a keyboard plugged into the port. Provisioning (`prov.*`) is accepted *only* over USB.
- **Wi-Fi (signed mode, default)** — plain WebSocket, every frame `{n, sid, m, s}` with
  `s = HMAC-SHA256(SHA-256(token), sid.n.m)`. The bridge issues a random session nonce `sid` in its first signed
  frame; counters are strictly increasing per direction, so captured frames cannot be replayed into a later session.
  The token itself never travels (the device identifies itself by id; the HMAC proves possession). Integrity and
  authenticity hold; confidentiality is the LAN's. Chosen because a classic ESP32 cannot hold a TLS session (~50 KB)
  next to the UI. Note: the bridge's stored token hash doubles as the HMAC key, so `devices.json` (0600) is a secret.
- **Wi-Fi (tls mode)** — WebSocket over TLS 1.2+. The device pins the bridge's certificate (mbedTLS verifies it as its CA);
  no CA store, no clock dependency beyond validity dates. Authentication is a 256-bit bearer token per device.
  Two ways to establish trust: (1) `aitl provision --bridge` over USB (cert + token delivered on the wire you are
  holding); (2) `aitl pair`: the device connects unpinned **only while you have explicitly started pairing on it**,
  receives the cert, and shows the certificate SHA-256 and a 6-digit code — pair only if both match the terminal.
  Pairing windows are 3 minutes, rate-limited, and closed after one success.
- **mDNS name** — the bridge advertises `aitl-bridge.local`; devices connect by name so the Mac's IP can change.
- **Bluetooth link (Nordic UART)** — the Mac connects to the device; the bridge treats a BLE connection it initiated
  like USB (trusted). The link is not encrypted unless the host bonds; keep it for home use, prefer Wi-Fi TLS elsewhere.
  Device-mic audio over BLE is ADPCM at 8 KB/s, which a classic ESP32 BLE 4.2 link sustains with MTU 247.
- **Claude Code hooks** — `aitl hooks install` adds PermissionRequest / Notification / Stop hooks that POST to
  127.0.0.1:7778 only; they fail open (bridge down = normal terminal prompt) and never run anything themselves.
- **Bluetooth HID** — standard BLE bonding with Secure Connections (no MITM pin: the device has no secure input).
  The host decides whether to trust the keyboard. Only ASCII keystrokes and media keys can be sent.

## Bridge hardening
- Binds Wi-Fi only with `--wifi`/`bridge.wifi = true`; serial-only by default.
- Per-device rate limits on HID actions and pairing attempts; max frame size 64 KB; `/ws` path only.
- A macOS notification announces the first HID action of each bridge run.
- No inbound command can read files, run shell, or change config — the device can only chat, type, press the
  allow-listed keys, toggle dictation, read the clipboard through the predefined actions, and adjust volume.
- Clipboard actions send clipboard contents to the selected provider. Pick a local Ollama model for sensitive text.

## Known limitations (be honest with yourself)
- NVS on the classic ESP32 is not encrypted unless you enable flash encryption; a lost device leaks its Wi-Fi
  password and its bridge token (not any API key). Revoke the token; it is useless without LAN access anyway.
- The Wi-Fi link is as private as your LAN plus TLS; TOFU pairing is as strong as your fingerprint comparison.
- `typing_method = paste` briefly places text on the Mac clipboard (restored afterwards).
