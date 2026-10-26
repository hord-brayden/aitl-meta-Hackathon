---
name: musecorder
description: Build, change, install and test applets (small JSON tools) on the Musecorder device - an ESP32 "CYD" touchscreen linked to this Mac by the musecorder bridge. Use whenever the user asks for a tool, counter, tally, clicker, timer, dashboard, logger, monitor, controller, gauge, dice, form or "app" on the device / Musecorder / ESP32 / CYD / tricorder, or wants to change what the device screen shows, read a GPIO pin, post readings to a webhook, or run something on the device. Also covers reading the device screen back, firing events, reading or setting applet state and logs through the musecorder MCP tools.
---

# Musecorder applets

An applet is one JSON document: a screen (`ui`), a flat key/value `state`, and handlers (`on`) that run small
action lists on taps, timers, GPIO edges and network replies. You write the JSON here on the Mac and push it to
the device through the `musecorder` MCP tools (no compiler, no reflash; install takes ~2 s and survives reboots).

Full spec: [reference/applets.md](reference/applets.md) (also available as the `applet_spec` MCP tool).
Examples: [examples/applets/](examples/applets/) - `door-tally`, `pomodoro`, `dice`, `pin-watch`.

## Workflow (always in this order)

1. **Check the link**: `device_status` must list a device. If the bridge is not running, tell the user to run
   `musecorder serve` in `bridge/` (the MCP tools talk to it on 127.0.0.1:7778).
2. **Write the JSON** (see Authoring rules). Put every URL, threshold and label in `state`.
3. **Validate + install**: `applet_install(json, open=true)`. It runs the schema validator first and returns
   errors without sending; fix them and retry. The device answers `applet.ack {ok, err, size, heap}`.
4. **Read the screen back**: `applet_screen` returns a plain-text rendering of what the user sees. Compare it
   with what you intended (labels, numbers, button texts).
5. **Test**: `applet_event(id, name)` runs a handler as if tapped; `applet_state_set(id, '{"k": v}')` changes a
   value; `applet_state_get(id)` reads it back; `applet_log(id)` shows `{"log": ...}` lines and errors
   (templates that failed render as `?` and log the reason).
6. **Iterate**: edit the JSON and `applet_install` again (same `id` replaces in place). Bump `version`.
7. Clean up test applets with `applet_remove(id)`; `applet_close` returns to the launcher.

Tell the user the applet id, what each button does, and what you changed on each iteration.

## Document skeleton

```json
{
  "id": "door-tally",          // [a-z0-9-]{1,24}: the handle for every tool call
  "name": "Door Tally",        // <= 20 chars, launcher tile
  "icon": "plus",              // LVGL symbol name or 1-2 chars
  "version": 1,
  "autostart": false,          // open on boot (kiosk)
  "background": false,         // keep timers/GPIO running when not on screen
  "state": { "count": 0, "url": "https://...", "status": "idle" },   // flat scalars only
  "persist": [ "count" ],      // keys that survive reboot
  "ui": [ ...widgets... ],
  "on": { "start": [ ...actions... ], "every:5": [ ... ], "gpio:22:fall": [ ... ], "myevent": [ ... ] }
}
```

Limits: document <= 12 KB, <= 48 widgets, <= 32 state keys, <= 16 handlers, <= 8 timers, <= 4 GPIO watches.

## Widgets (top to bottom; `row` lays children out horizontally)

| type | key fields |
|---|---|
| `label` | `text` (template), `size` s/m/l/xl/**xxl** (48 px numerals), `style` body/mono/muted/accent/warn/ok, `align`, `color` #rrggbb |
| `button` | `text`, `do` [actions], `hold` [actions on long press], `style` default/primary/danger/ghost, `grow`, `h` |
| `row` | `items` [widgets], `gap` |
| `toggle` | `text`, `bind` (bool key), `on_change` |
| `slider` | `bind` (number key), `min`, `max`, `on_change` (fires on release) |
| `bar` | `value` (template -> number), `min`, `max`, `color` |
| `input` | `bind` (string key), `placeholder`, `password`, `on_change` (opens the on-screen keyboard) |
| `log` | `bind` (string key), `lines` - monospace scrolling panel; feed it with `append` |
| `spacer` (`h`) / `divider` | layout |

Any widget may carry `"hidden": "${expr}"`. Every `text` is a template: `${expr}` re-renders on state change.

## Events (`on` keys)

`start`, `stop`, `every:N` (seconds, N >= 1), `gpio:P:rise|fall|change` (`:nopull` to drop the pull-up),
`boot:press` (BOOT button), `link` (bridge came up), any other name = custom event run by `{"event": "name"}`.

## Actions (one verb per object, run in order)

`set {k: v}` · `inc`/`dec "k"` or `{"k": 5}` · `toggle "k"` · `append {"k": "line ${x}", "max": 20}` ·
`toast "text"` · `led idle|green|red|amber|blue|thinking|off` · `beep {freq, ms}` ·
`gpio.mode {pin, mode: in|in_pullup|out}` · `gpio.write {pin, value}` · `gpio.read {pin, into}` · `adc.read {pin, into}` ·
`http {method GET|POST|PUT, url, headers, json|body, timeout<=15, via auto|bridge|direct, into, then[], else[]}` ·
`ask {prompt, into, then[], else[]}` (asks the bridge's current AI model) · `hid.type "text"` · `hid.key "cmd+tab"` ·
`notify {title, text}` (macOS) · `log "text"` · `event "name"` · `emit "name"` (to host scripts) ·
`{"if": "${expr}", "then": [...], "else": [...]}` (flat, not nested) · `save true` · `exit true` · `sleep ms` (<= 2000).

After `http`: `resp.status`, `resp.body` (<= 2 KB) and `resp.json.<field>` are readable; `err` in `else`.
A `set` value that is exactly one `${expr}` keeps the expression's type (number/bool); anything else is a string.

## Expressions `${ ... }`

Literals, state keys by name, built-ins (`device.id`, `device.name`, `device.heap`, `link.up`, `wifi.up`, `wifi.ip`,
`wifi.rssi`, `uptime`, `time` HH:MM, `date`, `epoch`, `millis`), operators `+ - * / % == != < <= > >= && || ! ?:`,
functions `len str num int round abs min max clamp floor ceil pad fmt upper lower trim contains startswith replace
substr random(lo,hi) hhmmss(s) json(body, "path")`. `+` concatenates when either side is a string. Unknown names
are `null` (prints empty); division by zero is 0; errors never crash the applet.

## Authoring rules

1. One screen, the fewest widgets that do the job. Readings people glance at go in an `xxl` label.
2. Put URLs, thresholds and labels in `state` so the user can change them with `applet_state_set` later.
3. Persist only tallies and settings, never timestamps or scratch values.
4. `background: true` only when a timer or pin must keep working while the user is elsewhere.
5. Network: `via: auto`; show progress in a status label; always handle `else`.
6. Never put secrets in an applet: use a webhook URL that embeds its own token, or route through the bridge.
7. Quote templates in JSON strings only; `${count > 0 ? 'yes' : 'no'}` is fine, nested braces must balance.
8. After every install: `applet_screen`, then `applet_event`/`applet_state_get` to prove the logic.

## Other device tools

`device_type_text(text)` types into the frontmost Mac app, `device_key("cmd+tab")` presses keys,
`device_set("brightness", "200")` changes a device setting (brightness, rotation, screenTimeout, bleLink, model, reboot).

## When the JSON route is not enough

If the applet needs a capability the engine lacks (a new sensor driver, a protocol), say so and point at the
firmware (`firmware/src/applets/`, native apps + reflash). The JSON route covers most "tricorder" jobs.
