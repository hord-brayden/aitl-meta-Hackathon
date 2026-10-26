"""macOS actions: typing, key combos, dictation, clipboard, media, notifications.

Everything goes through /usr/bin/osascript (System Events) so the bridge needs Accessibility permission for
the terminal/Python that runs it: System Settings > Privacy & Security > Accessibility.
"""
from __future__ import annotations
import asyncio
import shlex
import subprocess
import sys
from typing import Any

IS_MAC = sys.platform == "darwin"
LAST_ERROR = {"text": ""}          # last osascript error (for diagnostics / device toasts)


def accessibility_denied(err: str) -> bool:
    e = err.lower()
    return "assistive access" in e or "-1719" in e or "(1002)" in e or "not allowed to send keystrokes" in e or "-25211" in e

# System Events key codes (US layout)
KEYCODES = {
    "enter": 36, "return": 36, "tab": 48, "space": 49, "delete": 51, "backspace": 51, "esc": 53, "escape": 53,
    "left": 123, "right": 124, "down": 125, "up": 126, "home": 115, "end": 119, "pageup": 116, "pagedown": 121,
    "f1": 122, "f2": 120, "f3": 99, "f4": 118, "f5": 96, "f6": 97, "f7": 98, "f8": 100, "f9": 101, "f10": 109,
    "f11": 103, "f12": 111, "fn": 63, "rcmd": 54, "lcmd": 55, "ctrl": 59, "rctrl": 62, "forwarddelete": 117,
}
MODS = {"cmd": "command down", "command": "command down", "gui": "command down", "meta": "command down",
        "ctrl": "control down", "control": "control down", "alt": "option down", "opt": "option down",
        "option": "option down", "shift": "shift down"}


async def _osa(script: str, timeout: float = 20) -> tuple[int, str]:
    if not IS_MAC:
        return 1, "not macOS"
    proc = await asyncio.create_subprocess_exec("/usr/bin/osascript", "-e", script,
                                                stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout)
    except asyncio.TimeoutError:
        proc.kill()
        return 1, "timeout"
    rc = proc.returncode or 0
    text = (out or err).decode("utf-8", "replace").strip()
    if rc != 0:
        LAST_ERROR["text"] = text[:300]
    return rc, text


def _esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


async def frontmost_app() -> str:
    rc, out = await _osa('tell application "System Events" to get name of first process whose frontmost is true')
    return out if rc == 0 else ""


async def clipboard_get() -> str:
    if not IS_MAC:
        return ""
    proc = await asyncio.create_subprocess_exec("/usr/bin/pbpaste", stdout=asyncio.subprocess.PIPE)
    out, _ = await proc.communicate()
    return out.decode("utf-8", "replace")


async def clipboard_set(text: str) -> None:
    if not IS_MAC:
        return
    proc = await asyncio.create_subprocess_exec("/usr/bin/pbcopy", stdin=asyncio.subprocess.PIPE)
    await proc.communicate(text.encode("utf-8"))


async def type_text(text: str, method: str = "paste", restore_clipboard: bool = True) -> bool:
    """Type into the frontmost app. 'paste' is Unicode-safe and fast; 'keystroke' types char by char."""
    if not text:
        return True
    if method == "keystroke" and text.isascii():
        for chunk in (text[i:i + 500] for i in range(0, len(text), 500)):
            rc, _ = await _osa(f'tell application "System Events" to keystroke "{_esc(chunk)}"')
            if rc != 0:
                return False
        return True
    old = await clipboard_get() if restore_clipboard else None
    await clipboard_set(text)
    await asyncio.sleep(0.05)
    rc, _ = await _osa('tell application "System Events" to keystroke "v" using command down')
    if restore_clipboard and old is not None:
        await asyncio.sleep(0.35)
        await clipboard_set(old)
    return rc == 0


async def key_combo(spec: str) -> bool:
    parts = [p.strip().lower() for p in spec.split("+") if p.strip()]
    mods = [MODS[p] for p in parts if p in MODS]
    keys = [p for p in parts if p not in MODS]
    if not keys:
        # modifier-only taps (e.g. "rcmd"): press via key code
        for p in parts:
            if p in KEYCODES:
                rc, _ = await _osa(f'tell application "System Events" to key code {KEYCODES[p]}')
                if rc != 0:
                    return False
        return True
    key = keys[-1]
    using = f" using {{{', '.join(mods)}}}" if mods else ""
    if key in KEYCODES:
        rc, _ = await _osa(f'tell application "System Events" to key code {KEYCODES[key]}{using}')
    elif len(key) == 1:
        rc, _ = await _osa(f'tell application "System Events" to keystroke "{_esc(key)}"{using}')
    else:
        return False
    return rc == 0


async def dictation(op: str, state: dict[str, Any], method: str = "menu", shortcut: str = "fn fn") -> bool:
    """Start/stop/toggle macOS dictation in the frontmost app. Sets state["path"] to "menu" | "shortcut" | "denied"."""
    want = {"start": True, "stop": False}.get(op, not state.get("dictating", False))
    ok = False
    state["path"] = ""
    if method == "menu":
        item = "Start Dictation…" if want else "Stop Dictation"
        script = (
            'tell application "System Events"\n'
            '  set p to first process whose frontmost is true\n'
            '  tell p\n'
            '    try\n'
            f'      click (first menu item of menu "Edit" of menu bar 1 whose name starts with "{"Start Dictation" if want else "Stop Dictation"}")\n'
            '      return "ok"\n'
            '    on error\n'
            '      return "nomenu"\n'
            '    end try\n'
            '  end tell\n'
            'end tell'
        )
        rc, out = await _osa(script)
        if rc != 0 and accessibility_denied(out):
            state["path"] = "denied"; return False
        ok = rc == 0 and out == "ok"
        if ok:
            state["path"] = "menu"
        _ = item
    if not ok:  # no Edit > Start Dictation in this app: fall back to the configured shortcut (toggle semantics)
        taps = [t for t in shortcut.split() if t in KEYCODES and t != "fn"]   # a synthetic Fn press is ignored by macOS
        for t in taps:
            rc, out = await _osa(f'tell application "System Events" to key code {KEYCODES[t]}')
            if rc != 0:
                state["path"] = "denied" if accessibility_denied(out) else ""
                return False
            await asyncio.sleep(0.12)
        ok = bool(taps)
        if ok:
            state["path"] = "shortcut"
    if ok:
        state["dictating"] = want
    return ok


async def ask_by_voice(timeout: float = 120, shortcut: str = "fn fn", method: str = "menu") -> str:
    """Pop a native dialog, start dictation into it, return what the user said (empty if cancelled)."""
    script = (
        'tell application "System Events" to activate\n'
        'set r to display dialog "Speak, then press Ask." default answer "" with title "Musecorder · Ask by voice" '
        'buttons {"Cancel", "Ask"} default button "Ask"\n'
        'return text returned of r'
    )
    task = asyncio.create_task(_osa(script, timeout=timeout))
    await asyncio.sleep(0.8)
    await dictation("start", {"dictating": False}, method=method, shortcut=shortcut)
    rc, out = await task
    return out if rc == 0 else ""


async def now_playing() -> dict[str, Any]:
    """{"app": "Spotify"|"Music"|"", "state": "playing"|"paused"|"", "title", "artist"} without launching either app."""
    script = (
        'set out to ""\n'
        'if application "Spotify" is running then\n'
        '  tell application "Spotify"\n'
        '    try\n      set out to "Spotify|" & (player state as text) & "|" & (name of current track) & "|" & (artist of current track)\n    end try\n'
        '  end tell\n'
        'end if\n'
        'if out is "" and application "Music" is running then\n'
        '  tell application "Music"\n'
        '    try\n      set out to "Music|" & (player state as text) & "|" & (name of current track) & "|" & (artist of current track)\n    end try\n'
        '  end tell\n'
        'end if\n'
        'return out')
    rc, out = await _osa(script, timeout=6)
    if rc != 0 or not out:
        return {"app": "", "state": "", "title": "", "artist": ""}
    parts = (out.split("|") + ["", "", "", ""])[:4]
    return {"app": parts[0], "state": parts[1], "title": parts[2][:60], "artist": parts[3][:40]}


async def media(key: str) -> bool:
    if key == "vol_up":
        rc, _ = await _osa('set volume output volume ((output volume of (get volume settings)) + 10)')
    elif key == "vol_down":
        rc, _ = await _osa('set volume output volume ((output volume of (get volume settings)) - 10)')
    elif key == "mute":
        rc, _ = await _osa('set volume output muted (not (output muted of (get volume settings)))')
    elif key in ("next", "prev"):
        verb = "next track" if key == "next" else "previous track"
        rc, _ = await _osa(
            f'if application "Spotify" is running then\n tell application "Spotify" to {verb}\n'
            f'else\n tell application "Music" to {verb}\nend if')
    else:
        rc, _ = await _osa(
            'if application "Spotify" is running then\n tell application "Spotify" to playpause\n'
            'else\n tell application "Music" to playpause\nend if')
    return rc == 0


async def probe() -> dict[str, Any]:
    """Can this process drive System Events? (Accessibility permission for the bridge's own identity.)"""
    rc, out = await _osa('tell application "System Events" to keystroke ""', timeout=8)
    return {"keystrokes": rc == 0, "error": "" if rc == 0 else out[:200], "accessibility_denied": rc != 0 and accessibility_denied(out)}


async def notify(title: str, text: str) -> None:
    await _osa(f'display notification "{_esc(text)}" with title "{_esc(title)}"')


def accessibility_hint() -> str:
    return ("Grant Accessibility to 'Musecorder Bridge': System Settings > Privacy & Security > Accessibility > + > "
            "~/.config/musecorder/Musecorder Bridge.app (then musecorder stop; musecorder autostart on).")
