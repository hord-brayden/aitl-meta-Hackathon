"""Control plane: permission cards and admin route resolution."""
import asyncio

from musecorder import hooks


async def test_permission_roundtrip():
    sent = []
    async def bc(m): sent.append(m); return 1
    hs = hooks.HookServer(bc)
    task = asyncio.create_task(hs.ask_permission("bash", "rm -rf build", project="proj", session="abc", wait=2))
    await asyncio.sleep(0.05)
    alert = sent[0]
    assert alert["t"] == "alert" and alert["kind"] == "permission" and alert["title"] == "bash needs approval"
    assert "rm -rf build" in alert["text"] and alert["actions"] == ["Allow", "Deny"] and alert["project"] == "proj"
    assert hs.reply(alert["id"], "allow")
    assert await task == "allow"
    assert not hs.reply(alert["id"], "allow")                 # already resolved


async def test_permission_timeout_and_no_device():
    cleared = []
    async def bc(m): cleared.append(m["t"]); return 1
    hs = hooks.HookServer(bc)
    assert await hs.ask_permission("edit", "a.py", wait=0.05) is None
    assert "alert.clear" in cleared
    async def none(m): return 0
    assert await hooks.HookServer(none).ask_permission("bash", "x") is None


async def test_notice_queued_behind_permission():
    sent = []
    async def bc(m): sent.append(m); return 1
    hs = hooks.HookServer(bc)
    task = asyncio.create_task(hs.ask_permission("bash", "x", wait=2))
    await asyncio.sleep(0.02)
    await hs.notice("Finished", "done", kind="done")
    assert len(sent) == 1 and hs.queued
    hs.reply(sent[0]["id"], "deny")
    assert await task == "deny"
    assert sent[-1]["kind"] == "done" and not hs.queued


def test_route_table():
    assert hooks.route("GET", "/admin/status", b"") == ("status", {})
    assert hooks.route("GET", "/admin/applet/state?id=dice&x=1", b"") == ("applet_state_get", {"id": "dice", "x": "1"})
    assert hooks.route("GET", "/admin/applet/log?id=dice", b"") == ("applet_log", {"id": "dice"})
    assert hooks.route("POST", "/admin/applet/install", b'{"json": "{}", "open": true}') == ("applet_install", {"json": "{}", "open": True})
    assert hooks.route("POST", "/admin/applet/state", b'{"id": "d", "state": {"a": 1}}') == ("applet_state_set", {"id": "d", "state": {"a": 1}})
    assert hooks.route("POST", "/admin/type", b'{"text": "hi"}') == ("type", {"text": "hi"})
    assert hooks.route("POST", "/admin/key", b'{"combo": "enter"}') == ("key", {"combo": "enter"})
    assert hooks.route("GET", "/admin/applet/ui", b"") == ("applet_ui", {})
    assert hooks.route("POST", "/admin/applet/close", b"") == ("applet_close", {})
    assert hooks.route("GET", "/nope", b"") is None
    assert hooks.route("DELETE", "/admin/status", b"") is None
    assert hooks.route("POST", "/admin/applet/open", b"{bad")[1] == {"__bad_json__": True}
    for method, path in hooks.ROUTES:
        assert path.startswith("/admin/")
