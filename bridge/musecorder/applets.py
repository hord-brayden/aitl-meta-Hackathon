"""Applet documents (docs/APPLETS.md): a pure validator and the chunker used to push a document over the link.

No device, no network: importable from the CLI, the MCP server and tests alike.
"""
from __future__ import annotations
import json
import re
from pathlib import Path
from typing import Any

from . import protocol

# ---- limits (docs/APPLETS.md section 1) ----
MAX_DOC_BYTES = 12 * 1024
MAX_WIDGETS = 48
MAX_STATE_KEYS = 32
MAX_HANDLERS = 16
MAX_TIMERS = 8
MAX_GPIO_WATCHES = 4
MAX_NAME = 20
ID_RE = re.compile(r"^[a-z0-9-]{1,24}$")
CHUNK_CHARS = 4000            # applet.put `d` size
INLINE_LIMIT = 5000           # documents above this many bytes are chunked with applet.put

ICONS = {"plus", "ok", "wifi", "bell", "power", "play", "list", "edit", "charge", "gps", "audio", "image", "eye_open",
         "warning", "home", "refresh", "upload", "download", "settings", "keyboard", "bluetooth", "usb", "sd_card", "battery_full"}

COMMON_WIDGET_KEYS = {"type", "id", "hidden"}
WIDGETS: dict[str, set[str]] = {
    "label":   {"text", "size", "style", "align", "color"},
    "button":  {"text", "do", "hold", "style", "grow", "h"},
    "row":     {"items", "gap"},
    "toggle":  {"text", "bind", "on_change"},
    "slider":  {"bind", "min", "max", "on_change"},
    "bar":     {"value", "min", "max", "color"},
    "input":   {"bind", "placeholder", "password", "on_change"},
    "log":     {"bind", "lines"},
    "spacer":  {"h"},
    "divider": set(),
}
LABEL_SIZES = {"s", "m", "l", "xl", "xxl"}
LABEL_STYLES = {"body", "mono", "muted", "accent", "warn", "ok"}
ALIGNS = {"left", "center", "right"}
BUTTON_STYLES = {"default", "primary", "danger", "ghost"}
COLOR_RE = re.compile(r"^#[0-9a-fA-F]{6}$")

ACTIONS = {"set", "inc", "dec", "toggle", "append", "toast", "led", "beep", "gpio.mode", "gpio.write", "gpio.read",
           "adc.read", "http", "ask", "hid.type", "hid.key", "notify", "log", "event", "emit", "if", "save", "exit", "sleep"}
LEDS = {"idle", "green", "red", "amber", "blue", "thinking", "off"}
GPIO_MODES = {"in", "in_pullup", "out"}
HTTP_METHODS = {"GET", "POST", "PUT"}
HTTP_VIA = {"auto", "bridge", "direct"}
HTTP_KEYS = {"method", "url", "headers", "json", "body", "timeout", "via", "into", "then", "else"}
ASK_KEYS = {"prompt", "into", "then", "else"}

EVENT_RE = re.compile(r"^(start|stop|link|boot:press|every:(\d+)|gpio:(\d+):(rise|fall|change)(:nopull)?|[A-Za-z_][A-Za-z0-9_-]{0,31})$")
KEY_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,31}$")


class AppletError(ValueError):
    pass


def _is_scalar(v: Any) -> bool:
    return isinstance(v, (str, int, float, bool)) or v is None


def check_templates(value: Any, where: str, errors: list[str]) -> None:
    """Every string anywhere in the document is a template: `${` must be closed by a matching `}`."""
    if isinstance(value, str):
        depth, i = 0, 0
        while i < len(value):
            if value.startswith("${", i):
                depth += 1; i += 2; continue
            if value[i] == "}" and depth:
                depth -= 1
            i += 1
        if depth:
            errors.append(f"{where}: unbalanced '${{' in {value!r}")
    elif isinstance(value, list):
        for n, v in enumerate(value):
            check_templates(v, f"{where}[{n}]", errors)
    elif isinstance(value, dict):
        for k, v in value.items():
            check_templates(v, f"{where}.{k}", errors)


def _check_actions(actions: Any, where: str, errors: list[str], state_keys: set[str]) -> None:
    if not isinstance(actions, list):
        errors.append(f"{where}: must be a list of actions"); return
    for n, act in enumerate(actions):
        w = f"{where}[{n}]"
        if not isinstance(act, dict) or not act:
            errors.append(f"{w}: an action is an object with one verb key (got {act!r})"); continue
        if "if" in act:                                   # {"if": "${expr}", "then": [...], "else": [...]}
            extra = set(act) - {"if", "then", "else"}
            if extra:
                errors.append(f"{w}: if takes only if/then/else (got {sorted(extra)})")
            if not isinstance(act["if"], str):
                errors.append(f"{w}: the if condition must be a template string")
            for br in ("then", "else"):
                if br in act: _check_actions(act[br], f"{w}.{br}", errors, state_keys)
            continue
        if len(act) != 1:
            errors.append(f"{w}: an action is an object with exactly one verb key (got {sorted(act)})"); continue
        (verb, arg), = act.items()
        if verb not in ACTIONS:
            errors.append(f"{w}: unknown action {verb!r} (known: {', '.join(sorted(ACTIONS))})"); continue
        if verb == "set":
            if not isinstance(arg, dict) or not arg:
                errors.append(f"{w}: set takes an object of key -> value")
            else:
                for k in arg:
                    if not KEY_RE.match(str(k)): errors.append(f"{w}: bad state key {k!r}")
        elif verb in ("inc", "dec"):
            if isinstance(arg, dict):
                if len(arg) != 1 or not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in arg.values()):
                    errors.append(f"{w}: {verb} takes a key name or {{\"key\": number}}")
            elif not isinstance(arg, str):
                errors.append(f"{w}: {verb} takes a key name or {{\"key\": number}}")
        elif verb == "toggle":
            if not isinstance(arg, str): errors.append(f"{w}: toggle takes a key name")
        elif verb == "append":
            if not isinstance(arg, dict) or not any(k != "max" for k in arg):
                errors.append(f"{w}: append takes {{\"key\": \"line\", \"max\": N}}")
        elif verb == "toast":
            if not isinstance(arg, (str, dict)): errors.append(f"{w}: toast takes text or {{text, error}}")
        elif verb == "led":
            if not (arg in LEDS or (isinstance(arg, str) and "${" in arg)):
                errors.append(f"{w}: led must be one of {', '.join(sorted(LEDS))} (or a template)")
        elif verb == "beep":
            if not isinstance(arg, dict): errors.append(f"{w}: beep takes {{freq, ms}}")
        elif verb in ("gpio.mode", "gpio.write", "gpio.read", "adc.read"):
            if not isinstance(arg, dict) or "pin" not in arg:
                errors.append(f"{w}: {verb} needs a pin"); continue
            pin = arg["pin"]
            if not (isinstance(pin, int) and 0 <= pin <= 39): errors.append(f"{w}: pin must be an int 0-39")
            if verb == "gpio.mode" and arg.get("mode") not in GPIO_MODES: errors.append(f"{w}: mode must be in/in_pullup/out")
            if verb in ("gpio.read", "adc.read") and not isinstance(arg.get("into"), str): errors.append(f"{w}: needs \"into\" state key")
            if verb == "gpio.write" and "value" not in arg: errors.append(f"{w}: needs \"value\"")
        elif verb == "http":
            if not isinstance(arg, dict) or not isinstance(arg.get("url"), str):
                errors.append(f"{w}: http needs a url"); continue
            for k in arg:
                if k not in HTTP_KEYS: errors.append(f"{w}: unknown http field {k!r}")
            if str(arg.get("method", "GET")).upper() not in HTTP_METHODS: errors.append(f"{w}: method must be GET/POST/PUT")
            if arg.get("via", "auto") not in HTTP_VIA: errors.append(f"{w}: via must be auto/bridge/direct")
            if "json" in arg and "body" in arg: errors.append(f"{w}: use json or body, not both")
            if "timeout" in arg and not (isinstance(arg["timeout"], (int, float)) and 1 <= arg["timeout"] <= 15):
                errors.append(f"{w}: timeout must be 1-15 seconds")
            for br in ("then", "else"):
                if br in arg: _check_actions(arg[br], f"{w}.{br}", errors, state_keys)
        elif verb == "ask":
            if not isinstance(arg, dict) or not isinstance(arg.get("prompt"), str):
                errors.append(f"{w}: ask needs a prompt"); continue
            for k in arg:
                if k not in ASK_KEYS: errors.append(f"{w}: unknown ask field {k!r}")
            for br in ("then", "else"):
                if br in arg: _check_actions(arg[br], f"{w}.{br}", errors, state_keys)
        elif verb in ("hid.type", "hid.key", "log", "event", "emit"):
            if not isinstance(arg, str) or not arg: errors.append(f"{w}: {verb} takes a string")
        elif verb == "notify":
            if not isinstance(arg, dict) or not (arg.get("title") or arg.get("text")): errors.append(f"{w}: notify takes {{title, text}}")
        elif verb in ("save", "exit"):
            if arg is not True: errors.append(f"{w}: {verb} takes true")
        elif verb == "sleep":
            if not (isinstance(arg, int) and 0 <= arg <= 2000): errors.append(f"{w}: sleep takes 0-2000 ms")


def _check_widget(wd: Any, where: str, errors: list[str], state_keys: set[str], count: list[int]) -> None:
    count[0] += 1
    if not isinstance(wd, dict):
        errors.append(f"{where}: widget must be an object"); return
    t = wd.get("type")
    if t not in WIDGETS:
        errors.append(f"{where}: unknown widget type {t!r} (known: {', '.join(WIDGETS)})"); return
    for k in wd:
        if k not in WIDGETS[t] and k not in COMMON_WIDGET_KEYS:
            errors.append(f"{where}: unknown field {k!r} for {t}")
    if t == "label":
        if not isinstance(wd.get("text"), str): errors.append(f"{where}: label needs text")
        if "size" in wd and wd["size"] not in LABEL_SIZES: errors.append(f"{where}: size must be one of {', '.join(sorted(LABEL_SIZES))}")
        if "style" in wd and wd["style"] not in LABEL_STYLES: errors.append(f"{where}: style must be one of {', '.join(sorted(LABEL_STYLES))}")
        if "align" in wd and wd["align"] not in ALIGNS: errors.append(f"{where}: align must be left/center/right")
        if "color" in wd and not (isinstance(wd["color"], str) and COLOR_RE.match(wd["color"])): errors.append(f"{where}: color must be #rrggbb")
    elif t == "button":
        if not isinstance(wd.get("text"), str): errors.append(f"{where}: button needs text")
        if "style" in wd and wd["style"] not in BUTTON_STYLES: errors.append(f"{where}: style must be default/primary/danger/ghost")
        for k in ("do", "hold"):
            if k in wd: _check_actions(wd[k], f"{where}.{k}", errors, state_keys)
    elif t == "row":
        items = wd.get("items")
        if not isinstance(items, list) or not items:
            errors.append(f"{where}: row needs items")
        else:
            for n, it in enumerate(items):
                _check_widget(it, f"{where}.items[{n}]", errors, state_keys, count)
    elif t in ("toggle", "slider", "input", "log"):
        b = wd.get("bind")
        if not isinstance(b, str): errors.append(f"{where}: {t} needs bind")
        elif b not in state_keys: errors.append(f"{where}: bind {b!r} is not a state key")
        if "on_change" in wd: _check_actions(wd["on_change"], f"{where}.on_change", errors, state_keys)
    elif t == "bar":
        if "value" not in wd: errors.append(f"{where}: bar needs value")
        if "color" in wd and not (isinstance(wd["color"], str) and COLOR_RE.match(wd["color"])): errors.append(f"{where}: color must be #rrggbb")
    if "hidden" in wd and not isinstance(wd["hidden"], (str, bool)):
        errors.append(f"{where}: hidden must be a template string or bool")


def validate(doc: dict[str, Any] | str) -> list[str]:
    """Return a list of precise errors; empty means the document is valid."""
    errors: list[str] = []
    if isinstance(doc, str):
        text = doc
        try:
            doc = json.loads(text)
        except json.JSONDecodeError as e:
            return [f"invalid JSON: {e}"]
    else:
        text = json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
    if not isinstance(doc, dict):
        return ["document must be a JSON object"]
    size = len(text.encode("utf-8"))
    if size > MAX_DOC_BYTES:
        errors.append(f"document is {size} bytes; limit is {MAX_DOC_BYTES}")
    for k in ("id", "name", "ui"):
        if k not in doc:
            errors.append(f"missing required key {k!r}")
    known = {"id", "name", "icon", "version", "autostart", "background", "state", "persist", "ui", "on"}
    for k in doc:
        if k not in known:
            errors.append(f"unknown top-level key {k!r}")
    aid = doc.get("id")
    if aid is not None and not (isinstance(aid, str) and ID_RE.match(aid)):
        errors.append(f"id must match [a-z0-9-]{{1,24}} (got {aid!r})")
    name = doc.get("name")
    if name is not None and not (isinstance(name, str) and 1 <= len(name) <= MAX_NAME):
        errors.append(f"name must be a string of 1-{MAX_NAME} chars")
    icon = doc.get("icon")
    if icon is not None and not (isinstance(icon, str) and (icon in ICONS or 1 <= len(icon) <= 2)):
        errors.append(f"icon must be an LVGL symbol name ({', '.join(sorted(ICONS))}) or 1-2 characters")
    if "version" in doc and not (isinstance(doc["version"], int) and not isinstance(doc["version"], bool)):
        errors.append("version must be an int")
    for k in ("autostart", "background"):
        if k in doc and not isinstance(doc[k], bool):
            errors.append(f"{k} must be a bool")
    state = doc.get("state", {})
    state_keys: set[str] = set()
    if not isinstance(state, dict):
        errors.append("state must be an object")
    else:
        for k, v in state.items():
            if not KEY_RE.match(str(k)): errors.append(f"state key {k!r} must match [A-Za-z_][A-Za-z0-9_]*")
            if not _is_scalar(v): errors.append(f"state.{k}: values must be numbers, strings or booleans (no nesting)")
        state_keys = {str(k) for k in state}
        if len(state_keys) > MAX_STATE_KEYS: errors.append(f"{len(state_keys)} state keys; limit is {MAX_STATE_KEYS}")
    persist = doc.get("persist", [])
    if not isinstance(persist, list) or not all(isinstance(p, str) for p in persist):
        errors.append("persist must be a list of state key names")
    else:
        for p in persist:
            if p not in state_keys: errors.append(f"persist: {p!r} is not a state key")
    ui = doc.get("ui")
    count = [0]
    if ui is not None:
        if not isinstance(ui, list):
            errors.append("ui must be a list of widgets")
        else:
            for n, wd in enumerate(ui):
                _check_widget(wd, f"ui[{n}]", errors, state_keys, count)
            if count[0] > MAX_WIDGETS: errors.append(f"{count[0]} widgets; limit is {MAX_WIDGETS}")
    on = doc.get("on", {})
    if not isinstance(on, dict):
        errors.append("on must be an object of event -> actions")
    else:
        timers = gpios = 0
        for ev, acts in on.items():
            m = EVENT_RE.match(str(ev))
            if not m:
                errors.append(f"on: bad event name {ev!r}"); continue
            if m.group(2) is not None:
                timers += 1
                if int(m.group(2)) < 1: errors.append(f"on.{ev}: every:N needs N >= 1")
            if m.group(3) is not None:
                gpios += 1
                if not 0 <= int(m.group(3)) <= 39: errors.append(f"on.{ev}: pin must be 0-39")
            _check_actions(acts, f"on.{ev}", errors, state_keys)
        if len(on) > MAX_HANDLERS: errors.append(f"{len(on)} handlers; limit is {MAX_HANDLERS}")
        if timers > MAX_TIMERS: errors.append(f"{timers} timers; limit is {MAX_TIMERS}")
        if gpios > MAX_GPIO_WATCHES: errors.append(f"{gpios} GPIO watches; limit is {MAX_GPIO_WATCHES}")
    check_templates(doc, "$", errors)
    return errors


def load(path: str | Path) -> tuple[dict[str, Any], str]:
    """Read a file; return (document, compact JSON text). Raises AppletError on invalid JSON."""
    text = Path(path).read_text(encoding="utf-8")
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as e:
        raise AppletError(f"{path}: invalid JSON: {e}") from e
    return doc, compact(doc)


def compact(doc: dict[str, Any]) -> str:
    return json.dumps(doc, ensure_ascii=False, separators=(",", ":"))


def chunk_document(text: str, size: int = CHUNK_CHARS) -> list[str]:
    return [text[i:i + size] for i in range(0, len(text), size)] or [""]


def install_frames(applet_id: str, text: str, open_after: bool = True) -> list[dict[str, Any]]:
    """Frames that push one document: a single applet.install when it fits, else applet.put chunks + applet.install."""
    single = {"t": "applet.install", "id": applet_id, "open": bool(open_after), "json": text}
    if len(text.encode("utf-8")) <= INLINE_LIMIT and protocol.fits(single):
        return [single]
    parts = chunk_document(text)
    frames = [{"t": "applet.put", "id": applet_id, "i": i, "n": len(parts), "d": d} for i, d in enumerate(parts)]
    frames.append({"t": "applet.install", "id": applet_id, "open": bool(open_after)})
    return frames
