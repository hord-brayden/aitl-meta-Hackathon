# Screen map (320×240 landscape by default; every layout is flex-based and also works in portrait)

```
00 BOOT       [C·O] MUSECORDER / MUSE IN THE LOOP / motto / boot log / UNCLASSIFIED // OPEN SOURCE
SETUP 1-3     welcome → orientation (rotate until "THIS EDGE IS UP" reads right) → color swatches → calibration
01 HOME       status bar: brand · link chip (USB/WIFI + BLE/WiFi icons; amber until a bridge acks)
              chips: ▸ model (tap → 02) · chat title (tap → 03, long-press → new chat)
              transcript (scrolls, streams) + floating STOP / COPY / PASTE buttons
              dock: DICTATE · TYPE · APPS · SETUP
02 MODELS     grouped by provider; ✓ current; dimmed = key missing
03 CHATS      + New chat · list · long-press to delete
07 APPS       launcher: one tile per installed applet (tap → run, hold → remove) · "Mac actions ▸" → 04
04 ACTIONS    tiles from config.toml + Mac controls (volume, play/pause, Cmd-Tab, …)
APP           a running applet: back · name · ☰ (launcher) · the applet's widgets (docs/APPLETS.md). Hold BOOT 3 s to leave.
05 ASK/TYPE   textarea + keyboard; top row: ✕ · mic (toggle Mac dictation) · AI ▸ · MAC ▸
06 SETUP      CONNECTION · DISPLAY · TOUCH · STORAGE · ABOUT (scrolling list of rows)
07 PAIR       bridge name · 6-digit code · certificate fingerprint · Match,pair / Reject
CALIBRATE     3 targets (hold each 0.65 s) → verify screen with a dot under your finger → Looks right / Redo
```

Design language follows caeruli-obscuri.com: void/midnight backgrounds, caerulei accents, bone text, mono
section labels ("02 — MODELS"), amber for live/attention states, phosphor green for success.
Fonts: Montserrat 12/14/20/28/48 (LVGL built-in; 48 is the applet `xxl` numeral) + UNSCII 8/16 mono for labels. Touch targets ≥ 34 px.
