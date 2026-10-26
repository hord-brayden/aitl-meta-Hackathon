"""MCP server (stdio) so Muse Code can drive the device: `musecorder-mcp`.

Every tool is a thin wrapper over the bridge's loopback admin routes (hooks.py) and returns the device's reply as
JSON text. `applet_install` validates first (musecorder.applets) and never sends an invalid document.
Register with `musecorder muse-setup` (writes ~/.config/muse/settings.json and installs the skill).
"""
from __future__ import annotations
import json as _json
import os
from pathlib import Path

from mcp.server.fastmcp import FastMCP

from . import applets
from .hooks import admin_request

mcp = FastMCP("musecorder", instructions=(
    "Tools for the Musecorder device (an ESP32 touchscreen linked to this Mac by the musecorder bridge). "
    "Applets are small JSON documents (read applet_spec first). Workflow: write JSON -> applet_install "
    "-> applet_screen to read the screen back -> applet_event / applet_state_set to test -> applet_log."))

SPEC_CANDIDATES = [
    Path(__file__).resolve().parents[2] / "docs" / "APPLETS.md",                      # repo checkout
    Path.home() / ".agents" / "skills" / "musecorder" / "reference" / "applets.md",   # installed skill
    Path(os.environ.get("MUSECORDER_APPLET_SPEC", "")),
]


def _dump(res: dict) -> str:
    return _json.dumps(res, indent=2, ensure_ascii=False)


def spec_text() -> str:
    for p in SPEC_CANDIDATES:
        if p and p.is_file():
            return p.read_text(encoding="utf-8")
    return "docs/APPLETS.md not found; run `musecorder muse-setup` to install the skill copy."


@mcp.tool()
def device_status() -> str:
    """Which Musecorder devices are linked to the bridge (id, transport, firmware, model). Errors if the bridge is not running."""
    return _dump(admin_request("/admin/status"))


@mcp.tool()
def applet_spec() -> str:
    """The full applet authoring spec (docs/APPLETS.md): document shape, widgets, actions, events, expressions, limits."""
    return spec_text()


@mcp.tool()
def applet_validate(json: str) -> str:
    """Validate an applet document without sending it. Returns {"ok": true} or {"ok": false, "errors": [...]}."""
    errs = applets.validate(json)
    return _dump({"ok": not errs, "errors": errs})


@mcp.tool()
def applet_install(json: str, open: bool = True) -> str:
    """Validate and push an applet JSON document to the device (installs to flash and, with open=true, runs it).
    Returns the validator's errors without sending when the document is invalid; else the device's applet.ack."""
    errs = applets.validate(json)
    if errs:
        return _dump({"ok": False, "errors": errs, "sent": False})
    doc = _json.loads(json)
    return _dump(admin_request("/admin/applet/install", {"json": applets.compact(doc), "open": bool(open)}, timeout=25))


@mcp.tool()
def applet_list() -> str:
    """Applets installed on the device: [{id, name, version, size, autostart}], plus which one is running and free flash."""
    return _dump(admin_request("/admin/applet/list"))


@mcp.tool()
def applet_remove(id: str) -> str:
    """Delete an applet from the device's flash."""
    return _dump(admin_request("/admin/applet/remove", {"id": id}))


@mcp.tool()
def applet_open(id: str) -> str:
    """Open (run) an installed applet on the device screen."""
    return _dump(admin_request("/admin/applet/open", {"id": id}))


@mcp.tool()
def applet_close() -> str:
    """Leave the running applet and return to the launcher."""
    return _dump(admin_request("/admin/applet/close", {}))


@mcp.tool()
def applet_state_get(id: str = "") -> str:
    """Read an applet's current state object (default: the running applet)."""
    return _dump(admin_request(f"/admin/applet/state?id={id}" if id else "/admin/applet/state"))


@mcp.tool()
def applet_state_set(id: str, state_json: str) -> str:
    """Merge a partial state object (JSON text, e.g. {"count": 0, "sheet": "https://..."}) into an applet's state."""
    try:
        state = json_loads(state_json)
    except ValueError as e:
        return _dump({"ok": False, "error": f"state_json must be a JSON object: {e}"})
    return _dump(admin_request("/admin/applet/state", {"id": id, "state": state}))


@mcp.tool()
def applet_event(id: str, name: str) -> str:
    """Fire a named handler of an applet (like tapping a button that does {"event": name})."""
    return _dump(admin_request("/admin/applet/event", {"id": id, "name": name}))


@mcp.tool()
def applet_screen() -> str:
    """A plain-text rendering of what is on the device screen right now (applet.ui.get). Use it to check your work."""
    return _dump(admin_request("/admin/applet/ui"))


@mcp.tool()
def applet_log(id: str) -> str:
    """The applet's log lines: `lines` from the device ring buffer plus `bridge_lines` the bridge kept (last 200)."""
    return _dump(admin_request(f"/admin/applet/log?id={id}"))


@mcp.tool()
def device_type_text(text: str) -> str:
    """Type text into the frontmost Mac app (through the bridge's HID path)."""
    return _dump(admin_request("/admin/type", {"text": text}))


@mcp.tool()
def device_key(combo: str) -> str:
    """Press a key combo on the Mac, e.g. "cmd+tab", "enter", "f5"."""
    return _dump(admin_request("/admin/key", {"combo": combo}))


@mcp.tool()
def device_set(key: str, value: str) -> str:
    """Change a device setting (brightness, rotation, screenTimeout, bleLink, model, reboot ...). Values: on/off/ints/strings."""
    from .cli import parse_value, SETTABLE
    if key not in SETTABLE:
        return _dump({"ok": False, "error": f"unknown key; settable: {', '.join(SETTABLE)}"})
    v = parse_value(value)
    if SETTABLE[key].startswith("int") and isinstance(v, bool):
        v = int(v)
    return _dump(admin_request("/admin/set", {"key": key, "value": v}))


@mcp.resource("musecorder://applets/spec")
def applet_spec_resource() -> str:
    """docs/APPLETS.md"""
    return spec_text()


def json_loads(text: str):
    obj = _json.loads(text)
    if not isinstance(obj, dict):
        raise ValueError("not an object")
    return obj


def main() -> None:
    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
