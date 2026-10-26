"""Applet relay through Shared.admin with a fake device: send() records frames and auto-replies like the firmware.
Also covers net.http (httpx mocked), ai.ask and notify."""
import asyncio
import json
from pathlib import Path

import pytest

from musecorder import config, session as session_mod, applets, mac
from musecorder.providers.base import Provider, ModelInfo
from musecorder.session import Shared, DeviceSession


class FakeProvider(Provider):
    name = "fake"; label = "Fake"
    def available(self): return True
    def owns(self, model_id): return model_id.startswith("fake/")
    async def models(self): return [ModelInfo("fake/echo", "Echo", "Fake", True)]
    async def stream(self, model_id, system, messages, max_tokens):
        yield "answer: " + messages[-1]["content"]


@pytest.fixture
def shared(tmp_path: Path, monkeypatch):
    for name, val in (("CONFIG_DIR", tmp_path), ("CHATS_DIR", tmp_path / "chats"), ("TLS_DIR", tmp_path / "tls"),
                      ("DEVICES_FILE", tmp_path / "d.json"), ("CONFIG_FILE", tmp_path / "config.toml")):
        monkeypatch.setattr(config, name, val)
    cfg = config.load()
    cfg["bridge"]["default_model"] = "fake/echo"
    sh = Shared(cfg)
    sh.router.providers = [FakeProvider()]
    sh.chats.root = tmp_path / "chats"; sh.chats.root.mkdir(exist_ok=True)
    return sh


class FakeDevice:
    """Records what the bridge sends; replies the way the firmware does."""
    def __init__(self, shared: Shared, reply: bool = True):
        self.frames: list[dict] = []
        self.reply = reply
        self.buffer: dict[str, str] = {}
        self.session = DeviceSession(shared, self.send, transport="serial", peer="fake")
        self.session.device_id = "DEV1"; self.session.fw = "1.0"

    async def send(self, m: dict) -> None:
        self.frames.append(m)
        if not self.reply:
            return
        t = m["t"]
        if t == "applet.put":
            self.buffer[m["id"]] = self.buffer.get(m["id"], "") + m["d"]; return
        if t == "applet.install":
            text = m.get("json") or self.buffer.pop(m["id"], "")
            ok = bool(text) and json.loads(text).get("id") == m["id"]
            rep = {"t": "applet.ack", "id": m["id"], "ok": ok, "err": None if ok else "bad document", "size": len(text), "heap": 123456}
        elif t == "applet.list":
            rep = {"t": "applet.list", "applets": [{"id": "dice", "name": "Dice", "version": 1, "size": 400, "autostart": False}], "running": "dice", "onScreen": True, "free": 900000, "flashFree": 800000}
        elif t in ("applet.remove", "applet.open", "applet.close", "applet.state.set", "applet.event"):
            rep = {"t": "applet.ack", "id": m.get("id", ""), "ok": True}
        elif t == "applet.state.get":
            rep = {"t": "applet.state", "id": m.get("id", "dice"), "state": {"face": 4, "rolls": 2}}
        elif t == "applet.ui.get":
            rep = {"t": "applet.ui", "id": "dice", "text": "4\nroll 2\n[Roll]"}
        elif t == "applet.log.get":
            rep = {"t": "applet.log.list", "id": m["id"], "lines": ["booted", "rolled 4"]}
        else:
            return
        asyncio.get_running_loop().call_soon(lambda: asyncio.ensure_future(self.session.handle(rep)))


async def test_install_small_and_chunked(shared):
    dev = FakeDevice(shared)
    small = {"id": "dice", "name": "Dice", "ui": [{"type": "label", "text": "x"}]}
    res = await shared.admin("applet_install", {"json": json.dumps(small), "open": True})
    assert res["ok"] and res["id"] == "dice" and res["frames"] == 1 and res["heap"] == 123456
    assert dev.frames[0]["t"] == "applet.install" and dev.frames[0]["open"] is True
    big = {"id": "big", "name": "Big", "state": {"blob": "z" * 7000}, "ui": [{"type": "label", "text": "x"}]}
    res = await shared.admin("applet_install", {"json": json.dumps(big), "open": False})
    assert res["ok"] and res["frames"] == 3 and res["size"] == len(applets.compact(big))
    puts = [f for f in dev.frames if f["t"] == "applet.put"]
    assert len(puts) == 2 and puts[0]["n"] == 2


async def test_install_rejects_bad_input(shared):
    FakeDevice(shared)
    assert not (await shared.admin("applet_install", {"json": "{nope"}))["ok"]
    assert "id" in (await shared.admin("applet_install", {"json": json.dumps({"name": "x"})}))["error"]
    res = await shared.admin("applet_install", {"json": json.dumps({"id": "huge", "name": "H", "ui": [], "state": {"b": "x" * 13000}})})
    assert "exceeds" in res["error"]


async def test_list_state_ui_log_event(shared):
    dev = FakeDevice(shared)
    res = await shared.admin("applet_list", {})
    assert res["ok"] and res["applets"][0]["id"] == "dice" and res["running"] == "dice" and res["flashFree"] == 800000
    res = await shared.admin("applet_state_get", {"id": "dice"})
    assert res["state"]["face"] == 4
    res = await shared.admin("applet_state_get", {})
    assert dev.frames[-1] == {"t": "applet.state.get"}
    res = await shared.admin("applet_state_set", {"id": "dice", "state": '{"face": 1}'})
    assert res["ok"] and dev.frames[-1]["state"] == {"face": 1}
    assert not (await shared.admin("applet_state_set", {"id": "dice", "state": "[1]"}))["ok"]
    res = await shared.admin("applet_event", {"id": "dice", "name": "roll"})
    assert res["ok"] and dev.frames[-1] == {"t": "applet.event", "id": "dice", "name": "roll"}
    res = await shared.admin("applet_ui", {})
    assert res["text"].startswith("4\n") and res["id"] == "dice"
    await dev.session.handle({"t": "applet.log", "id": "dice", "line": "hello from device"})
    await dev.session.handle({"t": "applet.event.out", "id": "dice", "name": "done", "state": {"face": 6}})
    res = await shared.admin("applet_log", {"id": "dice"})
    assert res["lines"] == ["booted", "rolled 4"]
    assert len(res["bridge_lines"]) == 2 and "hello from device" in res["bridge_lines"][0] and "emit done" in res["bridge_lines"][1]
    for op in ("applet_remove", "applet_open"):
        assert (await shared.admin(op, {"id": "dice"}))["ok"]
        assert not (await shared.admin(op, {}))["ok"]
    assert (await shared.admin("applet_close", {}))["ok"]


async def test_no_device_and_timeout(shared, monkeypatch):
    res = await shared.admin("applet_list", {})
    assert not res["ok"] and "no device" in res["error"]
    FakeDevice(shared, reply=False)
    monkeypatch.setattr(session_mod, "APPLET_WAIT", 0.05)
    res = await shared.applet_request([{"t": "applet.list"}], "applet.list", timeout=0.05)
    assert not res["ok"] and "did not answer" in res["error"]
    assert shared.applet_waiters["applet.list"] == []


async def test_bridge_log_ring_is_capped(shared):
    for n in range(250):
        shared.applet_log_append("dice", f"line {n}")
    assert len(shared.applet_logs["dice"]) == 200 and shared.applet_logs["dice"][-1].endswith("line 249")


class FakeResponse:
    def __init__(self, status, text): self.status_code, self.text = status, text


class FakeClient:
    calls: list = []
    fail: Exception | None = None
    def __init__(self, **kw): self.kw = kw
    async def __aenter__(self): return self
    async def __aexit__(self, *a): return False
    async def request(self, method, url, headers=None, **kw):
        FakeClient.calls.append((method, url, headers, kw, self.kw))
        if FakeClient.fail:
            raise FakeClient.fail
        return FakeResponse(200, "B" * 5000)


async def _wait_reply(dev: FakeDevice, t: str, n: int = 50):
    for _ in range(n):
        await asyncio.sleep(0.01)
        hit = [f for f in dev.frames if f["t"] == t]
        if hit:
            return hit[-1]
    raise AssertionError(f"no {t} reply")


async def test_net_http(shared, monkeypatch):
    import httpx
    FakeClient.calls = []; FakeClient.fail = None
    monkeypatch.setattr(httpx, "AsyncClient", FakeClient)
    dev = FakeDevice(shared)
    await dev.session.handle({"t": "net.http", "req": 1, "method": "post", "url": "https://example.com/hook",
                              "headers": {"Authorization": "Bearer SECRET", "X-A": "1"}, "body": {"a": 1}, "timeout": 99})
    rep = await _wait_reply(dev, "net.http.reply")
    assert rep["req"] == 1 and rep["status"] == 200 and len(rep["body"]) == 2000 and rep["err"] is None
    method, url, headers, kw, ckw = FakeClient.calls[-1]
    assert method == "POST" and kw["json"] == {"a": 1} and headers["X-A"] == "1" and ckw["timeout"] == 15.0
    for bad in ("file:///etc/passwd", "ftp://x", "http://127.0.0.1:7778/admin/type", "http://localhost/x", "nonsense"):
        dev.frames.clear()
        await dev.session.handle({"t": "net.http", "req": 2, "method": "GET", "url": bad})
        rep = await _wait_reply(dev, "net.http.reply")
        assert rep["err"] and rep["status"] == 0, bad
    assert len(FakeClient.calls) == 1
    FakeClient.fail = httpx.ConnectError("boom")
    dev.frames.clear()
    await dev.session.handle({"t": "net.http", "req": 3, "method": "GET", "url": "https://example.com/"})
    rep = await _wait_reply(dev, "net.http.reply")
    assert rep["err"].startswith("ConnectError") and rep["status"] == 0


async def test_net_http_rate_limit(shared, monkeypatch):
    import httpx
    FakeClient.calls = []; FakeClient.fail = None
    monkeypatch.setattr(httpx, "AsyncClient", FakeClient)
    dev = FakeDevice(shared)
    shared.http_limiter = session_mod.RateLimiter(rate=2, burst=2)
    for n in range(4):
        await dev.session.handle({"t": "net.http", "req": n, "method": "GET", "url": "https://example.com/"})
    await asyncio.sleep(0.1)
    reps = [f for f in dev.frames if f["t"] == "net.http.reply"]
    assert len(reps) == 4 and sum(1 for r in reps if r["err"] and "rate" in r["err"]) == 2


async def test_ai_ask_and_notify(shared, monkeypatch):
    dev = FakeDevice(shared)
    dev.session.model = "fake/echo"
    await dev.session.handle({"t": "ai.ask", "req": 7, "prompt": "what is 2+2"})
    rep = await _wait_reply(dev, "ai.reply")
    assert rep["req"] == 7 and rep["text"] == "answer: what is 2+2" and rep["err"] is None
    assert shared.chats.list() == []                           # no chat persisted
    await dev.session.handle({"t": "ai.ask", "req": 8, "prompt": ""})
    rep = await _wait_reply(dev, "ai.reply")
    assert rep["err"] == "empty prompt"
    seen = []
    async def fake_notify(title, text): seen.append((title, text))
    monkeypatch.setattr(mac, "notify", fake_notify)
    await dev.session.handle({"t": "notify", "title": "Pomodoro", "text": "done"})
    assert seen == [("Pomodoro", "done")]


async def test_type_and_key_admin(shared, monkeypatch):
    async def fake_type(text, method, restore): return text == "hi"
    async def fake_key(combo): return combo == "cmd+tab"
    monkeypatch.setattr(mac, "type_text", fake_type); monkeypatch.setattr(mac, "key_combo", fake_key)
    assert (await shared.admin("type", {"text": "hi"}))["ok"]
    assert not (await shared.admin("type", {"text": ""}))["ok"]
    assert (await shared.admin("key", {"combo": "cmd+tab"}))["ok"]
    assert not (await shared.admin("key", {"combo": "nope"}))["ok"]
    assert (await shared.admin("status", {}))["devices"] == []
    assert not (await shared.admin("bogus", {}))["ok"]


def test_local_tz_posix_shape():
    tz = session_mod.local_tz_posix()
    assert tz and tz[0].isalpha() and any(ch.isdigit() for ch in tz)
