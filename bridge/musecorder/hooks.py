"""The bridge's control plane: a tiny loopback HTTP server (127.0.0.1:7778) plus the `admin_request()` client used
by the CLI and the MCP server, and the permission-card relay (tool approvals from Muse Code -> device -> decision).

Everything here is loopback-only. Admin routes are thin: they map a path to a `Shared.admin(op, data)` call.
"""
from __future__ import annotations
import asyncio
import json
import logging
import secrets
import urllib.parse
import urllib.request
from typing import Any, Awaitable, Callable

log = logging.getLogger("musecorder.hooks")
HOOK_PORT = 7778
PERMISSION_WAIT = 75          # seconds a permission card waits for Allow / Deny on the device

# (method, path) -> admin op. GET routes take their data from the query string, POST routes from the JSON body.
ROUTES: dict[tuple[str, str], str] = {
    ("GET", "/admin/status"): "status",
    ("GET", "/admin/get"): "get",
    ("GET", "/admin/probe"): "probe",
    ("POST", "/admin/set"): "set",
    ("POST", "/admin/provision"): "provision",
    ("POST", "/admin/pair_open"): "pair_open",
    ("POST", "/admin/type"): "type",
    ("POST", "/admin/key"): "key",
    ("POST", "/admin/applet/install"): "applet_install",
    ("GET", "/admin/applet/list"): "applet_list",
    ("POST", "/admin/applet/remove"): "applet_remove",
    ("POST", "/admin/applet/open"): "applet_open",
    ("POST", "/admin/applet/close"): "applet_close",
    ("GET", "/admin/applet/state"): "applet_state_get",
    ("POST", "/admin/applet/state"): "applet_state_set",
    ("POST", "/admin/applet/event"): "applet_event",
    ("GET", "/admin/applet/ui"): "applet_ui",
    ("GET", "/admin/applet/log"): "applet_log",
}


def route(method: str, target: str, body: bytes) -> tuple[str, dict[str, Any]] | None:
    """Resolve an HTTP request to (admin op, data) or None for 404."""
    path, _, query = target.partition("?")
    op = ROUTES.get((method.upper(), path))
    if not op:
        return None
    if method.upper() == "GET":
        data = {k: v[-1] for k, v in urllib.parse.parse_qs(query).items()}
    else:
        try:
            data = json.loads(body or b"{}")
        except json.JSONDecodeError:
            return op, {"__bad_json__": True}
        if not isinstance(data, dict):
            data = {"__bad_json__": True}
    return op, data


class HookServer:
    """Serves the admin routes and relays permission cards to the device(s)."""
    def __init__(self, broadcast: Callable[[dict[str, Any]], Awaitable[int]], port: int = HOOK_PORT):
        self.broadcast = broadcast
        self.port = port
        self.pending: dict[str, asyncio.Future] = {}
        self.admin: Callable[[str, dict[str, Any]], Awaitable[dict[str, Any]]] | None = None
        self.queued: list[dict[str, Any]] = []      # non-permission cards held while a permission is on screen

    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            req = await asyncio.wait_for(reader.readline(), 5)
            method, target, *_ = req.decode("latin1").split(" ") + ["", ""]
            length = 0
            while True:
                h = await asyncio.wait_for(reader.readline(), 5)
                if h in (b"\r\n", b"\n", b""):
                    break
                k, _, v = h.decode("latin1").partition(":")
                if k.strip().lower() == "content-length":
                    length = int(v.strip())
            if length > 2_000_000:
                raise ValueError("body too large")
            body = await asyncio.wait_for(reader.readexactly(length), 10) if length else b""
            status, payload = 404, {"error": "not found"}
            resolved = route(method, target, body)
            if resolved and self.admin:
                op, data = resolved
                if data.get("__bad_json__"):
                    status, payload = 400, {"error": "body must be a JSON object"}
                else:
                    status, payload = 200, await self.admin(op, data)
            out = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            writer.write(b"HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % (status, len(out)) + out)
            await writer.drain()
        except Exception as e:  # noqa: BLE001
            log.debug("admin request failed: %s", e)
        finally:
            writer.close()

    # ---------- permission cards ----------
    async def ask_permission(self, tool: str, detail: str, project: str = "", session: str = "", wait: float | None = None) -> str | None:
        """Show a permission card on the device; return "allow" / "deny", or None when nobody answered in time."""
        aid = secrets.token_hex(3)
        fut: asyncio.Future = asyncio.get_running_loop().create_future()
        self.pending[aid] = fut
        n = await self.broadcast({"t": "alert", "id": aid, "kind": "permission", "title": f"{tool} needs approval",
                                  "text": str(detail)[:400], "project": project[:24], "session": session[:36],
                                  "actions": ["Allow", "Deny"]})
        if n == 0:
            self.pending.pop(aid, None)
            return None
        try:
            return await asyncio.wait_for(fut, wait if wait is not None else PERMISSION_WAIT)
        except asyncio.TimeoutError:
            await self.broadcast({"t": "alert.clear", "id": aid})
            await self.broadcast({"t": "ui.toast", "msg": f"{tool} request expired"})
            return None
        finally:
            self.pending.pop(aid, None)
            await self._flush_queue()

    async def notice(self, title: str, text: str, kind: str = "notify", project: str = "", session: str = "") -> None:
        card = {"t": "alert", "id": secrets.token_hex(3), "kind": kind, "title": title[:40], "text": text[:400],
                "project": project[:24], "session": session[:36], "actions": ["OK"]}
        if self.pending:
            self.queued.append(card)                    # never cover a permission card
        else:
            await self.broadcast(card)

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
        log.info("admin: listening on 127.0.0.1:%d", self.port)
        async with server:
            await server.serve_forever()


def admin_request(path: str, data: dict | None = None, timeout: float = 12.0) -> dict:
    """Loopback call into the running bridge (CLI and MCP server). GET when data is None, else POST JSON."""
    try:
        if data is None:
            req = urllib.request.Request(f"http://127.0.0.1:{HOOK_PORT}{path}", method="GET")
        else:
            req = urllib.request.Request(f"http://127.0.0.1:{HOOK_PORT}{path}", data=json.dumps(data).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8") or "{}")
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": f"bridge not reachable on 127.0.0.1:{HOOK_PORT} ({type(e).__name__}); is `musecorder serve` running?"}
