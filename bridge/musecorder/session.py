"""One DeviceSession per connected device (serial, Wi-Fi or BLE). Handles the NDJSON protocol, streams chats,
relays applet traffic (docs/APPLETS.md section 6) and performs Mac actions. Transport-agnostic: a transport just
supplies send() and feeds inbound frames."""
from __future__ import annotations
import asyncio
import logging
import os
import time
from collections import deque
from pathlib import Path
from typing import Any, Awaitable, Callable
from urllib.parse import urlsplit

from . import __version__, config, mac, protocol, audio, stt, applets
from .chat_store import ChatStore, Chat
from .providers import ProviderError
from .router import Router
from .security import Registry, RateLimiter, cert_pem, cert_fingerprint, new_token, pairing_code

log = logging.getLogger("musecorder.session")
SendFn = Callable[[dict[str, Any]], Awaitable[None]]
APPLET_REPLY_TYPES = ("applet.ack", "applet.list", "applet.state", "applet.ui", "applet.log.list")
APPLET_WAIT = 8.0
HTTP_METHODS = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"}
LOOPBACK_HOSTS = {"localhost", "127.0.0.1", "::1", "0.0.0.0", "[::1]"}


def local_tz_posix() -> str:
    """The Mac's local timezone as a POSIX TZ string (e.g. EST5EDT,M3.2.0,M11.1.0), read from the TZif footer."""
    try:
        for cand in (Path("/etc/localtime"), Path(os.environ.get("TZDIR", "/usr/share/zoneinfo")) / os.environ.get("TZ", "")):
            if cand.is_file():
                data = cand.read_bytes()
                if data[:4] == b"TZif" and data.rstrip(b"\n").rfind(b"\n") >= 0:
                    footer = data.rstrip(b"\n").rsplit(b"\n", 1)[-1].decode("ascii", "replace").strip()
                    if footer and not footer.startswith("TZif"):
                        return footer
    except OSError:
        pass
    std = (time.tzname[0] or "UTC").replace(" ", "")
    hours = time.timezone / 3600                       # POSIX sign: west of UTC is positive
    off = f"{int(hours)}" if hours == int(hours) else f"{int(hours)}:{int(abs(hours) % 1 * 60):02d}"
    if time.daylight and time.tzname[1]:
        return f"{std}{off}{time.tzname[1]},M3.2.0,M11.1.0"
    return f"{std}{off}"


class Pairing:
    """Bridge-wide pairing window (Wi-Fi TOFU). Opened by `musecorder serve --allow-pairing` or `musecorder pair`."""
    def __init__(self):
        self.open_until = 0.0
        self.offers: dict[str, dict[str, Any]] = {}   # device id -> {code, token, name}

    def is_open(self) -> bool:
        return time.time() < self.open_until

    def open(self, seconds: int = 180) -> None:
        self.open_until = time.time() + seconds


class Shared:
    def __init__(self, cfg: dict[str, Any]):
        self.cfg = cfg
        self.router = Router(cfg)
        self.chats = ChatStore()
        self.registry = Registry()
        self.pairing = Pairing()
        self.hid_limiter = RateLimiter(rate=4, burst=12)
        self.pair_limiter = RateLimiter(rate=0.05, burst=3)
        acfg = cfg.get("applets", {})
        self.http_limiter = RateLimiter(rate=float(acfg.get("http_rate", 2)), burst=max(2, int(acfg.get("http_rate", 2))))
        self.bridge_name = config.bridge_name(cfg)
        self.dict_state: dict[str, Any] = {"dictating": False}
        self.hid_notified = False
        self.device_settings: dict[str, dict[str, Any]] = {}
        self.sessions: set["DeviceSession"] = set()
        self.hooks = None          # HookServer, attached by the CLI when serving
        self.pending_settings: dict[str, asyncio.Future] = {}
        self.applet_waiters: dict[str, list[asyncio.Future]] = {}
        self.applet_logs: dict[str, deque] = {}
        mc = self.router.muse_code
        if mc is not None:
            mc.approval_cb = self.ask_permission

    async def now_playing_loop(self) -> None:
        last: dict[str, Any] = {}
        while True:
            try:
                if self.sessions:
                    np = await mac.now_playing()
                    if np != last:
                        last = np
                        await self.broadcast({"t": "np", **np})
            except Exception as e:  # noqa: BLE001
                log.debug("now playing: %s", e)
            await asyncio.sleep(4)

    # ---------- permission relay (Muse Code approvals -> device) ----------
    async def ask_permission(self, tool: str, detail: str, session: str = "") -> str | None:
        if not self.hooks:
            return None
        wait = float(self.cfg["providers"].get("muse_code", {}).get("approval_wait", 75))
        return await self.hooks.ask_permission(tool, detail, project="muse code", session=session, wait=wait)

    # ---------- applet relay: bridge -> device with a reply ----------
    async def applet_request(self, frames: list[dict[str, Any]], reply_t: str, timeout: float = APPLET_WAIT) -> dict[str, Any]:
        """Send frames in order, wait for the device's `reply_t` frame. Returns the reply (minus `t`) plus `ok`."""
        fut: asyncio.Future = asyncio.get_running_loop().create_future()
        self.applet_waiters.setdefault(reply_t, []).append(fut)
        try:
            n = 0
            for fr in frames:
                if not protocol.fits(fr):
                    return {"ok": False, "error": f"frame {fr.get('t')} exceeds {protocol.MAX_FRAME} bytes"}
                n = await self.broadcast(fr)
                if n == 0:
                    return {"ok": False, "error": "no device linked"}
            try:
                reply = await asyncio.wait_for(fut, timeout)
            except asyncio.TimeoutError:
                return {"ok": False, "error": f"device did not answer with {reply_t} within {timeout:.0f}s"}
        finally:
            lst = self.applet_waiters.get(reply_t, [])
            if fut in lst:
                lst.remove(fut)
        out = {k: v for k, v in reply.items() if k != "t"}
        if "ok" not in out:
            out["ok"] = not out.get("err")
        return out

    def resolve_applet_reply(self, m: dict[str, Any]) -> bool:
        lst = self.applet_waiters.get(str(m.get("t")), [])
        if not lst:
            return False
        fut = lst.pop(0)
        if not fut.done():
            fut.set_result(m)
        return True

    def applet_log_append(self, applet_id: str, line: str) -> None:
        self.applet_logs.setdefault(applet_id[:24], deque(maxlen=200)).append(f"{time.strftime('%H:%M:%S')} {line[:300]}")

    async def admin(self, op: str, data: dict[str, Any]) -> dict[str, Any]:
        """Loopback admin ops (hooks.py routes -> here). Used by the CLI and the MCP server."""
        if op == "probe":
            return await mac.probe()
        if op == "get":
            fut: asyncio.Future = asyncio.get_running_loop().create_future()
            self.pending_settings["__dump__"] = fut
            n = await self.broadcast({"t": "settings.get"})
            if n == 0:
                return {"ok": False, "error": "no device linked"}
            try:
                await asyncio.wait_for(fut, 5)
            except asyncio.TimeoutError:
                return {"ok": False, "error": "device did not answer"}
            return {"ok": True, "devices": self.device_settings, "models": self.router.as_wire(), "bridge": {"name": self.bridge_name, "wifi_mode": self.cfg["bridge"].get("wifi_mode", "signed"), "ws_port": self.cfg["bridge"]["ws_port"]}}
        if op == "provision":
            usb = [x for x in self.sessions if x.transport == "serial" and x.authenticated]
            if not usb:
                return {"ok": False, "error": "no device on USB (provisioning only goes over the cable)"}
            sent = []
            if data.get("ssid"):
                for x in usb:
                    await x.send({"t": "prov.wifi", "ssid": data["ssid"], "pass": data.get("password", "")})
                sent.append("wifi")
            if data.get("bridge"):
                from .security import BRIDGE_DNS
                token = new_token()
                host = data.get("host") or BRIDGE_DNS      # name, not IP: survives the Mac changing networks
                mode = self.cfg["bridge"].get("wifi_mode", "signed")
                for x in usb:
                    await x.send({"t": "prov.bridge", "host": host, "port": int(self.cfg["bridge"]["ws_port"]), "token": token,
                                  "name": self.bridge_name, "mode": mode, "ca": cert_pem() if mode == "tls" else ""})
                for x in usb:
                    self.registry.add(x.device_id or "usb-provisioned", f"Musecorder {x.device_id}", token, via="usb", fw=x.fw)
                sent.append(f"bridge {host}:{self.cfg['bridge']['ws_port']} ({mode})")
            return {"ok": True, "sent": sent, "devices": [x.device_id for x in usb]}
        if op == "pair_open":
            self.pairing.open(int(data.get("seconds", 180)))
            return {"ok": True, "fingerprint": cert_fingerprint(), "seconds": int(data.get("seconds", 180))}
        if op == "status":
            return {"ok": True, "bridge": self.bridge_name, "version": __version__,
                    "devices": [{"id": s.device_id, "transport": s.transport, "model": s.model, "fw": s.fw} for s in self.sessions if s.authenticated and s.device_id]}
        if op == "set":
            key, value = str(data.get("key", "")), data.get("value")
            fut = asyncio.get_running_loop().create_future()
            self.pending_settings[key] = fut
            n = await self.broadcast({"t": "settings.set", "key": key, "value": value})
            if n == 0:
                self.pending_settings.pop(key, None); return {"ok": False, "error": "no device linked"}
            try:
                ok = await asyncio.wait_for(fut, 5)
            except asyncio.TimeoutError:
                self.pending_settings.pop(key, None); return {"ok": False, "error": "device did not acknowledge"}
            return {"ok": ok, "error": None if ok else "unknown key"}
        if op == "type":
            text = str(data.get("text", ""))[:8000]
            if not text:
                return {"ok": False, "error": "empty text"}
            ok = await mac.type_text(text, self.cfg["hid"]["typing_method"], self.cfg["hid"]["restore_clipboard"])
            return {"ok": ok, "error": None if ok else mac.accessibility_hint()}
        if op == "key":
            ok = await mac.key_combo(str(data.get("combo", ""))[:40])
            return {"ok": ok, "error": None if ok else mac.accessibility_hint()}
        if op.startswith("applet_"):
            return await self.admin_applet(op[len("applet_"):], data)
        return {"ok": False, "error": "unknown op"}

    async def admin_applet(self, op: str, data: dict[str, Any]) -> dict[str, Any]:
        aid = str(data.get("id", ""))[:24]
        if op == "install":
            text = data.get("json")
            if isinstance(text, (dict, list)):
                text = applets.compact(text)
            if not isinstance(text, str) or not text.strip():
                return {"ok": False, "error": "json (string) required"}
            try:
                import json as _json
                doc = _json.loads(text)
            except ValueError as e:
                return {"ok": False, "error": f"invalid JSON: {e}"}
            if not isinstance(doc, dict) or not isinstance(doc.get("id"), str) or not applets.ID_RE.match(doc["id"]):
                return {"ok": False, "error": "document needs an id matching [a-z0-9-]{1,24}"}
            text = applets.compact(doc)
            if len(text.encode("utf-8")) > applets.MAX_DOC_BYTES:
                return {"ok": False, "error": f"document exceeds {applets.MAX_DOC_BYTES} bytes"}
            frames = applets.install_frames(doc["id"], text, bool(data.get("open", True)))
            res = await self.applet_request(frames, "applet.ack", timeout=15)
            res.setdefault("id", doc["id"]); res["frames"] = len(frames); res["bytes"] = len(text.encode("utf-8"))
            return res
        if op == "list":
            return await self.applet_request([{"t": "applet.list"}], "applet.list")
        if op in ("remove", "open"):
            if not aid:
                return {"ok": False, "error": "id required"}
            return await self.applet_request([{"t": f"applet.{op}", "id": aid}], "applet.ack")
        if op == "close":
            return await self.applet_request([{"t": "applet.close"}], "applet.ack")
        if op == "state_get":
            fr: dict[str, Any] = {"t": "applet.state.get"}
            if aid:
                fr["id"] = aid
            return await self.applet_request([fr], "applet.state")
        if op == "state_set":
            state = data.get("state")
            if isinstance(state, str):
                try:
                    import json as _json
                    state = _json.loads(state)
                except ValueError as e:
                    return {"ok": False, "error": f"state must be a JSON object: {e}"}
            if not aid or not isinstance(state, dict):
                return {"ok": False, "error": "id and state (object) required"}
            return await self.applet_request([{"t": "applet.state.set", "id": aid, "state": state}], "applet.ack")
        if op == "event":
            name = str(data.get("name", ""))[:32]
            if not aid or not name:
                return {"ok": False, "error": "id and name required"}
            return await self.applet_request([{"t": "applet.event", "id": aid, "name": name}], "applet.ack")
        if op == "ui":
            return await self.applet_request([{"t": "applet.ui.get"}], "applet.ui")
        if op == "log":
            if not aid:
                return {"ok": False, "error": "id required"}
            res = await self.applet_request([{"t": "applet.log.get", "id": aid}], "applet.log.list")
            res["bridge_lines"] = list(self.applet_logs.get(aid, []))
            if not res.get("ok") and res["bridge_lines"]:
                res["ok"] = True; res.setdefault("lines", [])
            return res
        return {"ok": False, "error": f"unknown applet op {op!r}"}

    async def broadcast(self, msg: dict[str, Any]) -> int:
        n = 0
        for s in list(self.sessions):
            if s.authenticated and s.device_id:
                await s.send(msg); n += 1
        log.info("broadcast %s -> %d device(s)%s", msg.get("t"), n, "" if n else " (none linked: is the device showing 'USB' in green?)")
        return n


class DeviceSession:
    def __init__(self, shared: Shared, send: SendFn, transport: str, peer: str = ""):
        self.s = shared
        self._send = send
        self.transport = transport
        self.peer = peer
        self.device_id = ""
        self.fw = ""
        self.model = ""
        self.chat: Chat | None = None
        self.gen_task: asyncio.Task | None = None
        self.authenticated = transport == "serial"   # physical USB == trusted
        self.last_reply = ""
        self._audio: audio.AdpcmDecoder | None = None
        self._pcm = bytearray()
        self._audio_target = "ask"
        self._audio_seq = -1
        self._bg: set[asyncio.Task] = set()
        shared.sessions.add(self)

    async def send(self, msg: dict[str, Any]) -> None:
        try:
            await self._send(msg)
        except Exception as e:  # transport gone
            log.debug("send failed: %s", e)

    async def toast(self, text: str) -> None:
        await self.send({"t": "ui.toast", "msg": text[:200]})

    async def error(self, text: str) -> None:
        await self.send({"t": "chat.error", "msg": text[:300]})

    def _spawn(self, coro) -> None:
        t = asyncio.create_task(coro)
        self._bg.add(t)
        t.add_done_callback(self._bg.discard)

    # ---------- inbound ----------
    async def handle(self, m: dict[str, Any]) -> None:
        t = m.get("t", "")
        if t == "ping":
            await self.send({"t": "pong"}); return
        if t == "pong":
            return
        if t == "hello":
            await self.on_hello(m); return
        if t in ("pair.confirm", "pair.reject"):
            await self.on_pair_reply(m); return
        if not self.authenticated:
            await self.send({"t": "ui.toast", "msg": "Not paired"}); return
        handler = {
            "chat.send": self.on_chat_send, "chat.stop": self.on_chat_stop, "chat.new": self.on_chat_new,
            "chat.open": self.on_chat_open, "chat.delete": self.on_chat_delete, "chat.list": self.on_chat_list,
            "model.list": self.on_model_list, "model.set": self.on_model_set, "action.list": self.on_action_list,
            "action.run": self.on_action_run, "hid.type": self.on_hid_type, "hid.key": self.on_hid_key,
            "dictation": self.on_dictation, "hid.paste_last": self.on_paste_last, "clip.set_last": self.on_clip_last,
            "media": self.on_media, "prov.ok": self.on_prov_ok,
            "cc.list": self.on_cc_list, "cc.open": self.on_cc_open, "alert.reply": self.on_alert_reply,
            "audio.start": self.on_audio_start, "audio.chunk": self.on_audio_chunk, "audio.stop": self.on_audio_stop,
            "audio.cancel": self.on_audio_cancel, "settings.ack": self.on_settings_ack, "settings.dump": self.on_settings_dump,
            "applet.log": self.on_applet_log, "applet.event.out": self.on_applet_event_out,
            "net.http": self.on_net_http, "ai.ask": self.on_ai_ask, "notify": self.on_notify,
        }.get(t)
        if handler:
            await handler(m)
        elif t in APPLET_REPLY_TYPES:
            await self.on_applet_reply(m)
        else:
            log.info("unhandled %s", t)

    async def on_hello(self, m: dict[str, Any]) -> None:
        self.device_id = str(m.get("id", ""))[:32]
        self.fw = str(m.get("fw", ""))[:16]
        if not self.authenticated:
            await self.send({"t": "ui.toast", "msg": "Pairing required"}); return
        await self.s.router.refresh()
        wanted = m.get("model") or ""
        ok_ids = {mi.id for mi in self.s.router.catalog if mi.ok}
        self.model = wanted if wanted in ok_ids else self.s.router.pick_default()
        if m.get("chat"):
            self.chat = self.s.chats.get(str(m["chat"]))
        heap = int(m.get("heap") or 0)
        models = self.s.router.as_wire()
        if heap and heap < 70000:                      # low-RAM device (e.g. Wi-Fi on): only models it can use, fewer chats
            models = [x for x in models if x["ok"]][:12]
        ack = {"t": "hello.ack", "bridge": self.s.bridge_name, "version": __version__,
               "models": models, "chats": self.s.chats.list(8 if heap and heap < 70000 else 30), "actions": self.action_wire(),
               "model": self.model, "now": int(time.time()), "tz": local_tz_posix()}
        if self.chat:
            ack["chat"] = self.chat.id; ack["title"] = self.chat.title
        if not protocol.fits(ack):
            ack["models"] = ack["models"][:24]; ack["chats"] = ack["chats"][:12]
        if heap:
            log.info("device heap %d KB free", heap // 1024)
        await self.send(ack)
        log.info("device %s fw %s linked via %s (model %s)", self.device_id, self.fw, self.transport, self.model)
        app = await mac.frontmost_app()
        await self.send({"t": "status", "dictating": self.s.dict_state["dictating"], "app": app})
        await self.push_now_playing()

    # ---------- chats ----------
    def action_wire(self) -> list[dict[str, str]]:
        return [{"id": a["id"], "label": a["label"]} for a in self.s.cfg.get("actions", [])]

    async def on_chat_send(self, m: dict[str, Any]) -> None:
        text = str(m.get("text", "")).strip()
        if not text:
            return
        if m.get("model"):
            self.model = str(m["model"])
        cid = str(m.get("chat") or "")
        if cid and (not self.chat or self.chat.id != cid):
            self.chat = self.s.chats.get(cid)
        await self.generate(text)

    async def generate(self, user_text: str, type_result: bool = False) -> None:
        if self.gen_task and not self.gen_task.done():
            self.gen_task.cancel()
        self.gen_task = asyncio.create_task(self._generate(user_text, type_result))

    async def _generate(self, user_text: str, type_result: bool) -> None:
        if not self.chat:
            self.chat = self.s.chats.new(self.model)
        chat = self.chat
        chat.model = self.model
        chat.messages.append({"role": "user", "content": user_text})
        reply: list[str] = []
        buf = ""
        last_flush = time.monotonic()
        try:
            provider = self.s.router.resolve(self.model)
            system = self.s.cfg["bridge"]["system_prompt"]
            max_tokens = int(self.s.cfg["bridge"].get("max_output_tokens", 2048))
            history = chat.messages[-20:]
            if hasattr(provider, "session_id"):
                provider.session_id = chat.ext.get("muse_session") if chat.model == self.model else None
            async for piece in provider.stream(self.model, system, history, max_tokens):
                reply.append(piece); buf += piece
                now = time.monotonic()
                if len(buf) >= protocol.DELTA_CHUNK or now - last_flush > 0.12:
                    for c in protocol.chunks(buf):
                        await self.send({"t": "chat.delta", "d": c})
                    buf = ""; last_flush = now
            if buf:
                for c in protocol.chunks(buf):
                    await self.send({"t": "chat.delta", "d": c})
            full = "".join(reply)
            if getattr(provider, "last_session_id", None):
                chat.ext["muse_session"] = provider.last_session_id
            chat.messages.append({"role": "assistant", "content": full})
            self.last_reply = full
            if not chat.title:
                chat.title = (user_text.strip().splitlines()[0])[:40]
            self.s.chats.save(chat)
            await self.send({"t": "chat.done", "chat": chat.id, "title": chat.title})
            if type_result and full:
                await self._hid_guard()
                await mac.type_text(full.strip(), self.s.cfg["hid"]["typing_method"], self.s.cfg["hid"]["restore_clipboard"])
        except asyncio.CancelledError:
            partial = "".join(reply)
            if partial:
                chat.messages.append({"role": "assistant", "content": partial + " [stopped]"})
                self.s.chats.save(chat)
            await self.send({"t": "chat.done", "chat": chat.id, "title": chat.title})
            raise
        except ProviderError as e:
            chat.messages.pop()  # drop the user turn that failed
            await self.error(str(e))
        except Exception as e:  # noqa: BLE001
            log.exception("generation failed")
            chat.messages.pop()
            await self.error(f"bridge error: {type(e).__name__}: {str(e)[:120]}")

    async def on_chat_stop(self, m: dict[str, Any]) -> None:
        if self.gen_task and not self.gen_task.done():
            self.gen_task.cancel()

    async def on_chat_new(self, m: dict[str, Any]) -> None:
        self.chat = None

    async def on_chat_open(self, m: dict[str, Any]) -> None:
        chat = self.s.chats.get(str(m.get("chat", "")))
        if not chat:
            await self.toast("Chat not found"); return
        self.chat = chat
        if chat.model:
            self.model = chat.model
        self.last_reply = chat.last_assistant()
        await self.send({"t": "chat.view", "chat": chat.id, "title": chat.title,
                         "text": chat.transcript(2800), "last": self.last_reply[:1500]})

    async def on_chat_delete(self, m: dict[str, Any]) -> None:
        cid = str(m.get("chat", ""))
        self.s.chats.delete(cid)
        if self.chat and self.chat.id == cid:
            self.chat = None
        await self.on_chat_list(m)

    async def on_chat_list(self, m: dict[str, Any]) -> None:
        await self.send({"t": "chat.list", "chats": self.s.chats.list()})

    async def on_model_list(self, m: dict[str, Any]) -> None:
        await self.s.router.refresh()
        await self.send({"t": "model.list", "models": self.s.router.as_wire()[:40], "model": self.model})

    async def on_model_set(self, m: dict[str, Any]) -> None:
        self.model = str(m.get("model", self.model))[:64]
        await self.toast(f"Model: {self.model}")

    async def on_action_list(self, m: dict[str, Any]) -> None:
        await self.send({"t": "action.list", "actions": self.action_wire()})

    async def on_action_run(self, m: dict[str, Any]) -> None:
        aid = str(m.get("id", ""))
        action = next((a for a in self.s.cfg.get("actions", []) if a["id"] == aid), None)
        if not action:
            await self.error("Unknown action"); return
        if m.get("model"):
            self.model = str(m["model"])
        kind = action.get("kind", "clipboard")
        hid = self.s.cfg["hid"]
        if kind == "ask_voice":
            await self.toast("Speak into the dialog on your Mac...")
            said = await mac.ask_by_voice(shortcut=hid["dictation_shortcut"], method=hid["dictation"])
            if not said.strip():
                await self.error("Nothing captured"); return
            self.chat = self.chat or self.s.chats.new(self.model)
            await self.generate(said.strip()); return
        clip = (await mac.clipboard_get()).strip()
        if not clip:
            await self.error("Mac clipboard is empty"); return
        prompt = action.get("prompt", "{clip}").replace("{clip}", clip[:12000])
        self.chat = self.s.chats.new(self.model)      # actions always start a fresh chat
        await self.generate(prompt, type_result=(kind == "clipboard_type"))

    # ---------- HID ----------
    async def _hid_guard(self) -> bool:
        if not self.s.hid_limiter.allow(self.device_id or self.peer):
            await self.error("HID rate limit"); return False
        if self.s.cfg["hid"].get("notify") and not self.s.hid_notified:
            self.s.hid_notified = True
            await mac.notify("Musecorder linked", f"Your Musecorder device can now type, press keys and start dictation on this Mac ({self.transport}).")
        return True

    async def on_hid_type(self, m: dict[str, Any]) -> None:
        text = str(m.get("text", ""))[:8000]
        if not text or not await self._hid_guard():
            return
        ok = await mac.type_text(text, self.s.cfg["hid"]["typing_method"], self.s.cfg["hid"]["restore_clipboard"])
        log.info("hid.type %d chars -> %s %s", len(text), "ok" if ok else "FAILED", "" if ok else mac.LAST_ERROR["text"][:120])
        if not ok:
            await self.error(("Typing blocked by macOS. " if mac.accessibility_denied(mac.LAST_ERROR["text"]) else "Typing failed. ") + mac.accessibility_hint())
        else:
            await self.toast(f"Typed {len(text)} chars into {await mac.frontmost_app() or 'the Mac'}")

    async def on_hid_key(self, m: dict[str, Any]) -> None:
        if not await self._hid_guard():
            return
        ok = await mac.key_combo(str(m.get("combo", ""))[:40])
        log.info("hid.key %s -> %s", m.get("combo"), "ok" if ok else "FAILED")
        if not ok:
            await self.error("Key combo failed. " + mac.accessibility_hint())

    async def on_dictation(self, m: dict[str, Any]) -> None:
        if not await self._hid_guard():
            return
        hid = self.s.cfg["hid"]
        ok = await mac.dictation(str(m.get("op", "toggle")), self.s.dict_state, hid["dictation"], hid["dictation_shortcut"])
        app = await mac.frontmost_app()
        path = self.s.dict_state.get("path", "")
        log.info("dictation %s in %s -> %s via %s (now %s)", m.get("op"), app, "ok" if ok else "FAILED", path or "-", "on" if self.s.dict_state["dictating"] else "off")
        await self.send({"t": "status", "dictating": self.s.dict_state["dictating"], "app": app})
        if not ok:
            await self.error("Dictation blocked: " + mac.accessibility_hint() if path == "denied" else "Could not start dictation. " + mac.accessibility_hint())
        elif path == "shortcut" and self.s.dict_state["dictating"]:
            await self.toast(f"{app}: no Start Dictation menu, sent {hid['dictation_shortcut'].replace(' ', '+')}. If nothing opened: System Settings > Keyboard > Dictation > Shortcut = Press Control key twice")

    async def on_paste_last(self, m: dict[str, Any]) -> None:
        if not self.last_reply:
            await self.toast("No reply to paste"); return
        if await self._hid_guard():
            await mac.type_text(self.last_reply.strip(), self.s.cfg["hid"]["typing_method"], self.s.cfg["hid"]["restore_clipboard"])

    async def on_clip_last(self, m: dict[str, Any]) -> None:
        if self.last_reply:
            await mac.clipboard_set(self.last_reply.strip())

    async def on_media(self, m: dict[str, Any]) -> None:
        if await self._hid_guard():
            ok = await mac.media(str(m.get("key", "play_pause")))
            log.info("media %s -> %s", m.get("key"), "ok" if ok else "FAILED")
            await asyncio.sleep(0.4)
            await self.push_now_playing(force=True)

    # ---------- Muse Code sessions (wire names cc.list / cc.open kept for the firmware) ----------
    async def on_cc_list(self, m: dict[str, Any]) -> None:
        from . import muse_sessions
        items = [i.wire() for i in await muse_sessions.list_sessions(self.s.router.muse_code, limit=int(m.get("limit", 15)))]
        await self.send({"t": "cc.list", "sessions": items})

    async def on_cc_open(self, m: dict[str, Any]) -> None:
        """Bind (or reuse) a device chat to a Muse Code session and show what we know of it. Typing then resumes it."""
        from . import muse_sessions
        from .providers.muse_code_provider import PREFIX as MC
        sid = str(m.get("id", ""))[:36]
        mc = self.s.router.muse_code
        info = await muse_sessions.find(mc, sid)
        if not info:
            await self.toast("Session not found"); return
        chat = None
        for c in self.s.chats.list(100):
            cand = self.s.chats.get(c["id"])
            if cand and cand.ext.get("muse_session") == sid:
                chat = cand; break
        if not chat:
            chat = self.s.chats.new(MC + "default")
            chat.title = info.title[:40]
            chat.ext["muse_session"] = sid
        chat.model = chat.model if chat.model.startswith(MC) else MC + "default"
        self.model = chat.model
        text, last = "", ""
        if mc is not None:
            text, last = muse_sessions.transcript_from_read(await mc.read_session(sid))
        if not text:
            text = f"> (resumed Muse Code session {sid[:8]} • {info.project})\n"
        chat.messages = [{"role": "user", "content": f"(resumed Muse Code session {sid[:8]} • {info.project})"}, {"role": "assistant", "content": last or "(no reply yet)"}]
        self.s.chats.save(chat)
        self.chat = chat; self.last_reply = last
        await self.send({"t": "chat.view", "chat": chat.id, "title": chat.title, "text": text[:2800], "last": last})
        await self.send({"t": "model.list", "models": self.s.router.as_wire()[:40], "model": self.model})

    async def on_alert_reply(self, m: dict[str, Any]) -> None:
        aid, action = str(m.get("id", "")), str(m.get("action", "dismiss")).lower()
        if self.s.hooks and self.s.hooks.reply(aid, action):
            await self.s.broadcast({"t": "alert.clear", "id": aid})
            log.info("permission %s -> %s (device %s)", aid, action, self.device_id)

    # ---------- applets (docs/APPLETS.md section 6, device -> bridge) ----------
    async def on_applet_reply(self, m: dict[str, Any]) -> None:
        if not self.s.resolve_applet_reply(m):
            log.info("applet reply %s with no waiter: %s", m.get("t"), {k: v for k, v in m.items() if k not in ("t", "text", "lines")})

    async def on_applet_log(self, m: dict[str, Any]) -> None:
        aid, line = str(m.get("id", "?")), str(m.get("line", ""))
        self.s.applet_log_append(aid, line)
        log.info("applet %s: %s", aid, line[:200])

    async def on_applet_event_out(self, m: dict[str, Any]) -> None:
        aid = str(m.get("id", "?"))
        self.s.applet_log_append(aid, f"emit {m.get('name')} {m.get('state', '')}")
        log.info("applet %s emitted %s state=%s", aid, m.get("name"), str(m.get("state", ""))[:200])

    async def on_net_http(self, m: dict[str, Any]) -> None:
        self._spawn(self._net_http(m))

    async def _net_http(self, m: dict[str, Any]) -> None:
        import httpx
        req = m.get("req")
        acfg = self.s.cfg.get("applets", {})
        max_body = int(acfg.get("http_max_body", 2000))
        reply: dict[str, Any] = {"t": "net.http.reply", "req": req, "status": 0, "body": "", "err": None}
        url = str(m.get("url", ""))
        method = str(m.get("method", "GET")).upper()
        parts = urlsplit(url)
        host = (parts.hostname or "").lower()
        if parts.scheme not in ("http", "https") or not host:
            reply["err"] = "only http/https URLs are allowed"
        elif host in LOOPBACK_HOSTS or host.startswith("127."):
            reply["err"] = "loopback hosts are not allowed"
        elif method not in HTTP_METHODS:
            reply["err"] = f"method {method} not allowed"
        elif not self.s.http_limiter.allow("http:" + (self.device_id or self.peer)):
            reply["err"] = "rate limited (max 2 requests/s)"
        if reply["err"]:
            log.info("net.http %s %s -> refused: %s", method, url[:120], reply["err"])
            await self.send(reply); return
        headers = {str(k)[:64]: str(v)[:512] for k, v in (m.get("headers") or {}).items()} if isinstance(m.get("headers"), dict) else {}
        timeout = min(max(float(m.get("timeout") or 10), 1.0), float(acfg.get("http_timeout", 15)))
        body = m.get("body")
        kwargs: dict[str, Any] = {}
        if isinstance(body, (dict, list)):
            kwargs["json"] = body
        elif body is not None:
            kwargs["content"] = str(body)[:16000].encode("utf-8")
        safe_headers = {k: ("***" if k.lower() in ("authorization", "cookie", "x-api-key") else v) for k, v in headers.items()}
        log.info("net.http %s %s headers=%s", method, url[:160], safe_headers or "{}")
        try:
            async with httpx.AsyncClient(timeout=timeout, follow_redirects=True, max_redirects=3) as c:
                r = await c.request(method, url, headers=headers, **kwargs)
                reply["status"] = r.status_code
                reply["body"] = r.text[:max_body]
        except httpx.HTTPError as e:
            reply["err"] = f"{type(e).__name__}: {str(e)[:160]}"
        except Exception as e:  # noqa: BLE001
            reply["err"] = f"{type(e).__name__}: {str(e)[:160]}"
        log.info("net.http %s -> %s%s", url[:120], reply["status"], f" ({reply['err']})" if reply["err"] else "")
        await self.send(reply)

    async def on_ai_ask(self, m: dict[str, Any]) -> None:
        self._spawn(self._ai_ask(m))

    async def _ai_ask(self, m: dict[str, Any]) -> None:
        req = m.get("req")
        prompt = str(m.get("prompt", ""))[:4000].strip()
        reply: dict[str, Any] = {"t": "ai.reply", "req": req, "text": "", "err": None}
        if not prompt:
            reply["err"] = "empty prompt"
            await self.send(reply); return
        try:
            model = self.model or self.s.router.pick_default()
            provider = self.s.router.resolve(model)
            if hasattr(provider, "session_id"):
                provider.session_id = None                # one-shot: never touch a chat's Muse Code session
            system = self.s.cfg["bridge"]["system_prompt"] + " Reply in at most 3 short sentences."
            text = await asyncio.wait_for(provider.complete(model, system, prompt, int(self.s.cfg.get("applets", {}).get("ai_max_tokens", 512))), 90)
            reply["text"] = text[:2000]
        except asyncio.TimeoutError:
            reply["err"] = "model timed out"
        except ProviderError as e:
            reply["err"] = str(e)[:200]
        except Exception as e:  # noqa: BLE001
            log.exception("ai.ask failed")
            reply["err"] = f"{type(e).__name__}: {str(e)[:120]}"
        log.info("ai.ask %r -> %s", prompt[:60], reply["err"] or f"{len(reply['text'])} chars")
        await self.send(reply)

    async def on_notify(self, m: dict[str, Any]) -> None:
        title = str(m.get("title") or "Musecorder")[:100]
        text = str(m.get("text") or "")[:300]
        await mac.notify(title, text)
        log.info("notify: %s - %s", title, text[:80])

    # ---------- device microphone dictation ----------
    async def on_audio_start(self, m: dict[str, Any]) -> None:
        self._audio = audio.AdpcmDecoder(); self._pcm = bytearray(); self._audio_seq = -1
        self._audio_target = str(m.get("target", "ask"))
        log.info("audio: capture started (target %s, engine %s)", self._audio_target, stt.pick(self.s.cfg.get("stt", {})))

    async def on_audio_chunk(self, m: dict[str, Any]) -> None:
        if self._audio is None:
            return
        import base64
        try:
            data = base64.b64decode(m.get("d", ""))
        except ValueError:
            return
        seq = int(m.get("seq", 0))
        if self._audio_seq >= 0 and seq != self._audio_seq + 1:
            log.debug("audio: gap %d -> %d", self._audio_seq, seq)
        self._audio_seq = seq
        self._pcm += self._audio.decode(data)
        if len(self._pcm) > 16000 * 2 * 120:          # 2 minutes max
            await self.on_audio_stop({})

    async def on_audio_stop(self, m: dict[str, Any]) -> None:
        if self._audio is None:
            return
        pcm, target = bytes(self._pcm), self._audio_target
        self._audio = None; self._pcm = bytearray()
        secs = len(pcm) / 32000
        log.info("audio: %0.1fs captured, rms %0.0f", secs, audio.rms(pcm))
        if secs < 0.4 or audio.rms(pcm) < 60:
            await self.send({"t": "dictation.text", "text": "", "target": target, "note": "nothing heard (check mic wiring: SCK 22, WS 21, SD 35, L/R to GND)"})
            return
        await self.send({"t": "ui.toast", "msg": f"Transcribing {secs:0.1f}s..."})
        try:
            text = await stt.transcribe(audio.pcm_to_wav(pcm), self.s.cfg.get("stt", {}))
        except stt.SttError as e:
            await self.error(str(e)); return
        except Exception as e:  # noqa: BLE001
            log.exception("stt failed"); await self.error(f"Transcription failed: {type(e).__name__}"); return
        log.info("audio: transcript %r", text[:80])
        await self.send({"t": "dictation.text", "text": text[:4000], "target": target})
        if not text:
            return
        if target == "type":
            await self._hid_guard()
            await mac.type_text(text, self.s.cfg["hid"]["typing_method"], self.s.cfg["hid"]["restore_clipboard"])
        elif target == "ask":
            await self.generate(text)

    async def on_audio_cancel(self, m: dict[str, Any]) -> None:
        self._audio = None; self._pcm = bytearray()
        log.info("audio: capture cancelled (no speech)")

    async def on_settings_dump(self, m: dict[str, Any]) -> None:
        self.s.device_settings[self.device_id or self.peer] = {k: v for k, v in m.items() if k != "t"}
        fut = self.s.pending_settings.pop("__dump__", None)
        if fut and not fut.done():
            fut.set_result(True)

    async def on_settings_ack(self, m: dict[str, Any]) -> None:
        fut = self.s.pending_settings.pop(str(m.get("key", "")), None)
        if fut and not fut.done():
            fut.set_result(bool(m.get("ok")))

    async def push_now_playing(self, force: bool = False) -> None:
        np = await mac.now_playing()
        await self.send({"t": "np", **np})

    async def on_prov_ok(self, m: dict[str, Any]) -> None:
        log.info("device confirmed provisioning: %s", m.get("what"))

    # ---------- Wi-Fi pairing (TOFU with code + fingerprint confirmation) ----------
    async def offer_pairing(self) -> None:
        key = self.peer or "?"
        if not self.s.pair_limiter.allow(key):
            await self.send({"t": "ui.toast", "msg": "Pairing rate-limited"}); return
        if not self.s.pairing.is_open():
            await self.send({"t": "ui.toast", "msg": "Bridge pairing window is closed. Run: musecorder pair"}); return
        code, token = pairing_code(), new_token()
        self.s.pairing.offers[key] = {"code": code, "token": token, "at": time.time()}
        fp = cert_fingerprint()
        print(f"\n  PAIRING REQUEST from {key}\n  code:        {code}\n  fingerprint: {fp}\n  Confirm on the device only if both match.\n")
        await self.send({"t": "pair.offer", "ca": cert_pem(), "fp": fp, "code": code, "token": token, "name": self.s.bridge_name})

    async def on_pair_reply(self, m: dict[str, Any]) -> None:
        offer = self.s.pairing.offers.pop(self.peer or "?", None)
        if m.get("t") == "pair.reject" or not offer:
            print("  pairing rejected / no offer")
            return
        if str(m.get("code")) != offer["code"] or time.time() - offer["at"] > 180:
            print("  pairing code mismatch or expired"); return
        self.s.registry.add(self.device_id or self.peer, f"Musecorder {self.device_id}", offer["token"], via="wifi", fw=self.fw)
        self.s.pairing.open_until = 0
        print(f"  paired device {self.device_id}. It will reconnect with the pinned certificate.")

    async def close(self) -> None:
        self.s.sessions.discard(self)
        if self.gen_task and not self.gen_task.done():
            self.gen_task.cancel()
        for t in list(self._bg):
            t.cancel()
