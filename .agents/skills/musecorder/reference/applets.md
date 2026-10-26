# Musecorder applets

An **applet** is a small JSON document that describes a complete, self-contained tool for the device: its screen,
its state, and what happens on taps, timers, pins and network replies. Muse (or you) writes the JSON on a host
machine and pushes it to the device over the bridge link; the device validates it, stores it in flash
(`/applets/<id>.json` on LittleFS) and runs it immediately. **No compiler, no reflash, no reboot.** Installing an
applet takes about two seconds over USB and survives power cycles.

Applets run inside the firmware's applet engine (`firmware/src/applets/`). When an applet needs a capability the
engine does not have (a new sensor driver, a protocol), Muse extends the firmware instead: see `../AGENTS.md`
(native apps + reflash). The JSON route is the fast path and covers most "tricorder" jobs.

## 1. Document shape

```json
{
  "id": "door-tally",
  "name": "Door Tally",
  "icon": "plus",
  "version": 1,
  "autostart": false,
  "background": true,
  "state":   { "count": 0, "door": "A", "sheet": "https://script.google.com/macros/s/XXX/exec", "status": "idle" },
  "persist": [ "count" ],
  "ui": [
    { "type": "label", "text": "DOOR ${door}", "style": "mono" },
    { "type": "label", "text": "${count}", "size": "xl", "align": "center" },
    { "type": "row", "items": [
      { "type": "button", "text": "-1", "do": [ { "dec": "count" } ] },
      { "type": "button", "text": "+1", "style": "primary", "grow": 2, "do": [ { "inc": "count" }, { "led": "green" } ] }
    ]},
    { "type": "button", "text": "Post now", "do": [ { "event": "post" } ] },
    { "type": "label", "text": "${status}", "style": "muted", "align": "center" }
  ],
  "on": {
    "start":      [ { "set": { "status": "ready" } } ],
    "every:300":  [ { "event": "post" } ],
    "gpio:22:fall": [ { "inc": "count" } ],
    "post": [
      { "set": { "status": "posting..." } },
      { "http": { "method": "POST", "url": "${sheet}", "json": { "door": "${door}", "count": "${count}", "device": "${device.id}" },
                  "then": [ { "set": { "status": "posted ${count} at ${time}" } } ],
                  "else": [ { "set": { "status": "post failed: ${err}" } }, { "led": "red" } ] } }
    ]
  }
}
```

| key | type | meaning |
|---|---|---|
| `id` | `[a-z0-9-]{1,24}` | file name and the handle every command uses |
| `name` | string ≤ 20 | launcher tile and screen title |
| `icon` | string | an LVGL symbol name (`plus`, `ok`, `wifi`, `bell`, `power`, `play`, `list`, `edit`, `charge`, `gps`, `audio`, `image`, `eye_open`, `warning`, `home`, `refresh`, `upload`, `download`, `settings`, `keyboard`, `bluetooth`, `usb`, `sd_card`, `battery_full`) or 1–2 characters of text |
| `version` | int | yours; shown in `applet.list` |
| `autostart` | bool | open this applet on boot (kiosk). Hold BOOT for 3 s to leave any applet |
| `background` | bool | keep timers and GPIO handlers running when the applet is not on screen (default false) |
| `state` | object | initial values: numbers, strings, booleans (no nesting) |
| `persist` | string[] | state keys written to flash when they change (debounced 2 s) and restored on open |
| `ui` | widget[] | the screen, top to bottom |
| `on` | object | event name → action list |

Limits: document ≤ 12 KB, ≤ 48 widgets, ≤ 32 state keys, ≤ 16 handlers, ≤ 8 timers, ≤ 4 GPIO watches. The device
rejects anything larger with a reason in `applet.ack`.

## 2. Widgets

Every widget may carry `"hidden": "${expr}"` (hidden when truthy) and `"id": "w1"` (for `set_text`-style actions).
`text` fields are **templates**: `${expr}` is evaluated and substituted on every state change.

| type | fields | notes |
|---|---|---|
| `label` | `text`, `size` (`s` `m` `l` `xl` `xxl`), `style` (`body` `mono` `muted` `accent` `warn` `ok`), `align` (`left` `center` `right`), `color` (`#rrggbb`) | `xxl` is a 48 px numeral font for counters and readings |
| `button` | `text`, `do` (actions), `hold` (actions on long press), `style` (`default` `primary` `danger` `ghost`), `grow`, `h` (px) | tap feedback and LED blink are automatic |
| `row` | `items` (widgets), `gap` | horizontal flex; children with `grow` share width |
| `toggle` | `text`, `bind` (bool state key), `on_change` (actions) | switch |
| `slider` | `bind` (number key), `min`, `max`, `on_change` | fires on release |
| `bar` | `value` (template → number), `min`, `max`, `color` | progress / level bar |
| `input` | `bind` (string key), `placeholder`, `password` (bool), `on_change` | tap opens the on-screen keyboard |
| `log` | `bind` (string key), `lines` | monospace scrolling panel showing the last `lines` lines of the string |
| `chart` | `bind` (string key holding comma-separated numbers), `points` (2–120), `min`, `max`, `h`, `kind` (`line` \| `bar`), `color` | plots the last `points` values; append readings with `set` (`"series": "${series + ',' + raw}"`) and trim with `substr` |
| `spacer` | `h` | vertical gap, default: flex grow |
| `divider` | | thin rule |

## 3. Events (`on`)

| event key | fires when |
|---|---|
| `start` | applet opened (after persisted state is restored) |
| `stop` | applet left |
| `every:N` | every N seconds (N ≥ 1); only while open unless `background` is true |
| `gpio:P:rise` / `gpio:P:fall` / `gpio:P:change` | debounced (25 ms) edge on pin P, pulled up by default; `gpio:P:fall:nopull` to disable the pull-up |
| `boot:press` | the BOOT button tapped (the push-to-talk default is suspended while the applet defines this) |
| `link` | bridge link came up |
| any other name | a custom event raised with `{ "event": "name" }` (from the UI, a timer, or the bridge) |

Handlers are action lists. Actions run in order on the UI thread; network actions are asynchronous and continue in
their `then` / `else` lists.

## 4. Actions

One verb per object. Any string value is a template.

| action | effect |
|---|---|
| `{"set": {"k": "${expr}", "k2": "text"}}` | assign; a template that is exactly one `${expr}` keeps the expression's type (number / bool), anything else becomes a string |
| `{"inc": "k"}` / `{"inc": {"k": 5}}` / `{"dec": "k"}` | add / subtract (default 1) |
| `{"toggle": "k"}` | flip a bool |
| `{"append": {"k": "line ${x}", "max": 20}}` | append a line to a string key (for `log` widgets), keeping the last `max` lines |
| `{"toast": "text"}` | transient message; `{"toast": {"text": "...", "error": true}}` |
| `{"led": "idle\|green\|red\|amber\|blue\|thinking\|off"}` | status LED |
| `{"beep": {"freq": 880, "ms": 60}}` | piezo on GPIO 26 (boards that have one) |
| `{"gpio.mode": {"pin": 22, "mode": "in\|in_pullup\|out"}}` | configure a pin |
| `{"gpio.write": {"pin": 22, "value": "${armed ? 1 : 0}"}}` | drive a pin |
| `{"gpio.read": {"pin": 22, "into": "btn"}}` | read a pin into state (0/1) |
| `{"adc.read": {"pin": 34, "into": "raw"}}` | read an ADC pin (0–4095) into state |
| `{"i2c.scan": {"into": "found"}}` | scan the user I²C bus (SDA IO22 on the P3 header, SCL IO26, 100 kHz; IO21 is the touch INT line); `found` gets `"0x44,0x3c"` |
| `{"i2c.read": {"addr": 68, "reg": 0, "len": 2, "into": "t"}}` | read `len` (≤ 32) bytes, optionally after writing a register byte; `t` gets the first ≤ 4 bytes as a big-endian number, `t_hex` the whole payload as hex |
| `{"i2c.write": {"addr": 68, "reg": 36, "bytes": [1, 2]}}` | write a register byte (optional) followed by `bytes` |
| `{"http": {"method": "GET\|POST\|PUT", "url": "...", "headers": {}, "json": {} \| "body": "...", "timeout": 10, "via": "auto\|bridge\|direct", "into": "resp", "then": [], "else": []}}` | HTTP request. `via: auto` (default) uses the bridge when it is linked, otherwise the device's own Wi-Fi. On completion `resp.status`, `resp.body` (≤ 2 KB, text) and, for a JSON body, `resp.json.<field>` are readable in templates; `err` holds the failure text in `else` |
| `{"ask": {"prompt": "Summarize: ${log}", "into": "answer", "then": [], "else": []}}` | ask the current AI model through the bridge; the answer lands in state |
| `{"hid.type": "text"}` / `{"hid.key": "cmd+tab"}` | type / press keys on the host (bridge or BLE keyboard, per device settings) |
| `{"notify": {"title": "...", "text": "..."}}` | macOS notification through the bridge |
| `{"log": "text"}` | append to the applet log on the device (ring of 40 lines) and forward to the bridge (`applet.log`) |
| `{"event": "name"}` | run another handler |
| `{"if": "${expr}", "then": [], "else": []}` | branch |
| `{"save": true}` | persist now (otherwise debounced) |
| `{"exit": true}` | leave the applet |

## 5. Expressions

Inside `${ ... }`:

- Literals: `12`, `3.5`, `"text"`, `'text'`, `true`, `false`, `null`
- State keys by name: `count`, `door`
- Built-ins: `device.id`, `device.name`, `device.heap`, `device.board`, `link.up`, `wifi.up`, `wifi.ip`, `wifi.rssi`, `uptime` (s), `time` (`HH:MM`), `date` (`YYYY-MM-DD`), `epoch` (0 when no clock), `millis`
- Operators: `+ - * / %`, `== != < <= > >=`, `&& || !`, `? :`, parentheses. `+` concatenates when either side is a string.
- Functions: `len(s)`, `str(x)`, `num(x)`, `int(x)`, `round(x, d)`, `abs`, `min`, `max`, `clamp(x, lo, hi)`, `floor`, `ceil`, `pad(x, width)`, `fmt(x, decimals)`, `upper(s)`, `lower(s)`, `trim(s)`, `contains(s, sub)`, `startswith`, `replace(s, a, b)`, `substr(s, start, len)`, `random(lo, hi)`, `hhmmss(seconds)`, `json(resp.body, "path.to.field")`

Division by zero yields `0`; unknown names yield `null` (prints as empty). Errors never crash the applet: the
offending template renders as `?` and the error goes to the applet log.

## 6. Link protocol (bridge ⇄ device)

Added to `docs/PROTOCOL.md`. Frames are the usual NDJSON objects.

Bridge → device:

| t | fields | reply |
|---|---|---|
| `applet.put` | `id`, `i`, `n`, `d` (chunk of the JSON text, ≤ 4000 chars) | — (device assembles chunks) |
| `applet.install` | `id`, `open` (bool), `json` (whole document when it fits in one frame; otherwise omitted after `applet.put`) | `applet.ack {id, ok, err, size}` |
| `applet.list` | | `applet.list {applets:[{id,name,version,size,autostart}], running, free}` |
| `applet.remove` | `id` | `applet.ack` |
| `applet.open` | `id` | `applet.ack` |
| `applet.close` | | `applet.ack` |
| `applet.state.get` | `id` (optional, default running) | `applet.state {id, state}` |
| `applet.state.set` | `id`, `state` (partial) | `applet.ack` |
| `applet.event` | `id`, `name` | `applet.ack` |
| `applet.ui.get` | | `applet.ui {id, text}`: a plain-text rendering of the current screen (what a user sees) so an agent can check its work |
| `applet.log.get` | `id` | `applet.log.list {id, lines}` |
| `net.http.reply` | `req`, `status`, `body`, `err` | — |
| `ai.reply` | `req`, `text`, `err` | — |

Device → bridge:

| t | fields |
|---|---|
| `applet.ack` / `applet.list` / `applet.state` / `applet.ui` / `applet.log.list` | as above |
| `applet.log` | `id`, `line` |
| `applet.event.out` | `id`, `name`, `state` — raised by `{"emit": "name"}` so a host script can react |
| `net.http` | `req`, `method`, `url`, `headers`, `body`, `timeout` |
| `ai.ask` | `req`, `prompt` |
| `notify` | `title`, `text` |

## 7. Authoring rules for agents

1. Keep it small: one screen, the fewest widgets that do the job. Numbers people read from across a room go in an `xxl` label.
2. Put every URL, threshold and label in `state` so the user can change it later with `applet.state.set` instead of a reinstall.
3. Persist only what must survive a reboot (tallies, settings). Do not persist timestamps or scratch values.
4. Use `background: true` only when a timer or pin must keep working while the user is elsewhere.
5. Network: prefer `via: auto`. Show progress in a status label; handle `else`.
6. After installing, call `applet.ui.get` and read the screen back; then `applet.state.get` after a test tap or `applet.event`.
7. Never put secrets in an applet; use a webhook URL that embeds its own token (Google Apps Script, Zapier, n8n) or route through the bridge.
