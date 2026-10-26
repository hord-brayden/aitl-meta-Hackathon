"""Claude Code hook relay: a loopback HTTP server inside the bridge plus the tiny `aitl hook` client that Claude Code
runs. PermissionRequest hooks block (up to a timeout) until the device answers Allow / Deny; Notification and Stop
hooks become toasts. Everything is 127.0.0.1 only and fails open: if the bridge is not running, Claude Code behaves
exactly as before."""
from __future__ import annotations
import asyncio
import json
import logging
import os
import secrets
import sys
import time
import urllib.request
from pathlib import Path
from typing import Any, Awaitable, Callable

log = logging.getLogger("musecorder.hooks")
HOOK_PORT = 7778
PERMISSION_WAIT = 75          # seconds the hook waits for the device before falling back to the terminal prompt
SETTINGS = Path.home() / ".claude" / "settings.json"
EVENTS = {"PermissionRequest": PERMISSION_WAIT + 10, "Notification": 5, "Stop": 5}


class HookServer:
    """Receives hook payloads, hands alerts to a broadcaster, resolves permission decisions from alert replies."""
    def __init__(self, broadcast: Callable[[dict[str, Any]], Awaitable[int]], port: int = HOOK_PORT):
        self.broadcast = broadcast
        self.port = port
        self.pending: dict[str, asyncio.Future] = {}
        self.admin = None                              # async (op, data) -> dict, attached by the CLI
        self.queued: list[dict[str, Any]] = []      # non-permission cards held while a permission is on screen

    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            req = await asyncio.wait_for(reader.readline(), 5)
            method, path, *_ = req.decode("latin1").split(" ") + ["", ""]
            length = 0
            while True:
                h = await asyncio.wait_for(reader.readline(), 5)
                if h in (b"\r\n", b"\n", b""):
                    break
                k, _, v = h.decode("latin1").partition(":")
                if k.strip().lower() == "content-length":
                    length = int(v.strip())
            body = await asyncio.wait_for(reader.readexactly(length), 5) if length else b""
            status, payload = 404, {}
            if method == "POST" and path.startswith("/hook/"):
                event = path[len("/hook/"):]
                data = json.loads(body or b"{}")
                status, payload = 200, await self.on_event(event, data)
            elif method == "POST" and path == "/admin/set" and self.admin:
                data = json.loads(body or b"{}")
                status, payload = 200, await self.admin("set", data)
            elif method == "GET" and path == "/admin/status" and self.admin:
                status, payload = 200, await self.admin("status", {})
            elif method == "GET" and path == "/admin/get" and self.admin:
                status, payload = 200, await self.admin("get", {})
            elif method == "GET" and path == "/admin/probe" and self.admin:
                status, payload = 200, await self.admin("probe", {})
            elif method == "POST" and path == "/admin/provision" and self.admin:
                status, payload = 200, await self.admin("provision", json.loads(body or b"{}"))
            elif method == "POST" and path == "/admin/pair_open" and self.admin:
                status, payload = 200, await self.admin("pair_open", json.loads(body or b"{}"))
            out = json.dumps(payload).encode()
            writer.write(b"HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % (status, len(out)) + out)
            await writer.drain()
        except Exception as e:  # noqa: BLE001
            log.debug("hook request failed: %s", e)
        finally:
            writer.close()

    async def on_event(self, event: str, d: dict[str, Any]) -> dict[str, Any]:
        sid = str(d.get("session_id", ""))[:36]
        project = Path(str(d.get("cwd", ""))).name[:24]
        if event == "PermissionRequest":
            tool = str(d.get("tool_name", "tool"))
            ti = d.get("tool_input") or {}
            detail = ti.get("command") or ti.get("file_path") or ti.get("path") or ti.get("url") or json.dumps(ti)[:200]
            aid = secrets.token_hex(3)
            fut: asyncio.Future = asyncio.get_running_loop().create_future()
            self.pending[aid] = fut
            n = await self.broadcast({"t": "alert", "id": aid, "kind": "permission", "title": f"{tool} needs approval",
                                      "text": str(detail)[:400], "project": project, "session": sid,
                                      "actions": ["Allow", "Deny"]})
            if n == 0:
                self.pending.pop(aid, None)
                return {}
            try:
                action = await asyncio.wait_for(fut, PERMISSION_WAIT)
            except asyncio.TimeoutError:
                await self.broadcast({"t": "alert.clear", "id": aid})
                await self.broadcast({"t": "ui.toast", "msg": f"{tool} request expired; answer it in the terminal"})
                return {}                                   # fall back to the normal terminal prompt
            finally:
                self.pending.pop(aid, None)
                await self._flush_queue()
            if action == "allow":
                return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {"behavior": "allow"}}}
            if action == "deny":
                return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": {"behavior": "deny", "message": "Denied from the AITL device"}}}
            return {}
        if event == "Notification":
            kind = str(d.get("notification_type") or d.get("type") or "")
            msg = str(d.get("message") or "")[:200]
            if kind in ("permission_prompt",) and self.pending:
                return {}                                   # already shown as a permission alert
            card = {"t": "alert", "id": secrets.token_hex(3), "kind": "notify", "title": f"Claude Code • {project or 'session'}",
                    "text": msg or kind, "project": project, "session": sid, "actions": ["OK"]}
            if self.pending:
                self.queued.append(card)                    # never cover a permission card
            else:
                await self.broadcast(card)
            return {}
        if event == "Stop":
            last = str(d.get("last_assistant_message") or "").strip()[:400]
            card = {"t": "alert", "id": secrets.token_hex(3), "kind": "done", "title": f"Finished • {project or 'session'}",
                    "text": last or "Claude finished its turn.", "project": project, "session": sid, "actions": ["OK"]}
            if self.pending:
                self.queued.append(card)
            else:
                await self.broadcast(card)
            return {}
        return {}

    async def _flush_queue(self) -> None:
        if self.pending or not self.queued:
            return
        card = self.queued.pop(0)
        self.queued.clear()                                 # keep only the newest waiting notice
        await self.broadcast(card)

    def reply(self, alert_id: str, action: str) -> bool:
        fut = self.pending.get(alert_id)
        if fut and not fut.done():
            fut.set_result(action)
            return True
        return False

    async def run(self) -> None:
        server = await asyncio.start_server(self._handle, "127.0.0.1", self.port)
        log.info("hooks: listening on 127.0.0.1:%d (musecorder hooks install)", self.port)
        async with server:
            await server.serve_forever()


# ---------------- client side (what Claude Code runs) ----------------
def hook_client(event: str) -> int:
    """Read the hook payload from stdin, relay it to the bridge, print the decision (if any). Never fails Claude Code."""
    raw = sys.stdin.read()
    timeout = EVENTS.get(event, 5)
    try:
        req = urllib.request.Request(f"http://127.0.0.1:{HOOK_PORT}/hook/{event}", data=raw.encode("utf-8"),
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8")
        if body and body != "{}":
            sys.stdout.write(body)
    except Exception:  # bridge down / timeout: fall through silently
        pass
    return 0


def _hook_entry(event: str, timeout: int) -> dict[str, Any]:
    # Prefer the private runtime in ~/.config/musecorder (outside iCloud Drive, non-editable install): the project venv's
    # editable install can disappear when iCloud evicts files, which makes every hook print an import error.
    runtime = Path.home() / ".config" / "aitl" / "venv" / "bin" / "python3"
    exe = runtime if runtime.exists() else Path(sys.executable)
    return {"type": "command", "command": str(exe), "args": ["-m", "musecorder.cli", "hook", event], "timeout": timeout}


def install() -> str:
    SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    cfg = json.loads(SETTINGS.read_text() or "{}") if SETTINGS.exists() else {}
    hooks = cfg.setdefault("hooks", {})
    for event, timeout in EVENTS.items():
        groups = [g for g in hooks.get(event, []) if not _is_ours(g)]
        groups.append({"matcher": "*", "hooks": [_hook_entry(event, timeout)]} if event == "PermissionRequest" else {"hooks": [_hook_entry(event, timeout)]})
        hooks[event] = groups
    backup = SETTINGS.with_suffix(".json.bak")
    if SETTINGS.exists():
        backup.write_text(SETTINGS.read_text())
    SETTINGS.write_text(json.dumps(cfg, indent=2) + "\n")
    return f"installed hooks for {', '.join(EVENTS)} in {SETTINGS} (backup: {backup.name}). Restart open Claude Code sessions."


def uninstall() -> str:
    if not SETTINGS.exists():
        return "no settings file"
    cfg = json.loads(SETTINGS.read_text() or "{}")
    hooks = cfg.get("hooks", {})
    for event in list(hooks):
        hooks[event] = [g for g in hooks[event] if not _is_ours(g)]
        if not hooks[event]:
            del hooks[event]
    SETTINGS.write_text(json.dumps(cfg, indent=2) + "\n")
    return "removed AITL hooks"


def _is_ours(group: dict) -> bool:
    return any("musecorder" in " ".join([h.get("command", "")] + h.get("args", [])) for h in group.get("hooks", []))


def status() -> str:
    if not SETTINGS.exists():
        return "not installed"
    cfg = json.loads(SETTINGS.read_text() or "{}")
    have = [e for e, groups in cfg.get("hooks", {}).items() if any(_is_ours(g) for g in groups)]
    return f"installed: {', '.join(have)}" if have else "not installed"


def admin_request(path: str, data: dict | None = None, timeout: float = 8.0) -> dict:
    """Loopback call into the running bridge (used by `musecorder set` / `musecorder status`)."""
    try:
        if data is None:
            req = urllib.request.Request(f"http://127.0.0.1:{HOOK_PORT}{path}", method="GET")
        else:
            req = urllib.request.Request(f"http://127.0.0.1:{HOOK_PORT}{path}", data=json.dumps(data).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8") or "{}")
    except Exception as e:  # noqa: BLE001
        return {"error": f"bridge not reachable on 127.0.0.1:{HOOK_PORT} ({type(e).__name__}); is `musecorder serve` running?"}
