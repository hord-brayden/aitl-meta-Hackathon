import asyncio, json
from pathlib import Path
from musecorder import hooks, claude_sessions, importers


async def test_permission_roundtrip(monkeypatch):
    sent = []
    async def bc(m): sent.append(m); return 1
    hs = hooks.HookServer(bc)
    monkeypatch.setattr(hooks, "PERMISSION_WAIT", 2)
    task = asyncio.create_task(hs.on_event("PermissionRequest", {"session_id": "abc", "tool_name": "Bash", "tool_input": {"command": "rm -rf build"}, "cwd": "/x/proj"}))
    await asyncio.sleep(0.05)
    alert = sent[0]; assert alert["kind"] == "permission" and "rm -rf build" in alert["text"] and alert["project"] == "proj"
    assert hs.reply(alert["id"], "allow")
    out = await task
    assert out["hookSpecificOutput"]["decision"]["behavior"] == "allow"


async def test_permission_timeout_falls_through(monkeypatch):
    async def bc(m): return 1
    hs = hooks.HookServer(bc); monkeypatch.setattr(hooks, "PERMISSION_WAIT", 0.05)
    assert await hs.on_event("PermissionRequest", {"tool_name": "Edit", "tool_input": {"file_path": "a.py"}}) == {}


async def test_no_device_means_no_block():
    async def bc(m): return 0
    hs = hooks.HookServer(bc)
    assert await hs.on_event("PermissionRequest", {"tool_name": "Bash", "tool_input": {}}) == {}


def test_session_listing(tmp_path: Path, monkeypatch):
    proj = tmp_path / "-Users-me-proj"; proj.mkdir()
    f = proj / "11111111-2222-3333-4444-555555555555.jsonl"
    lines = [{"type": "user", "cwd": "/Users/me/proj", "sessionId": f.stem, "message": {"role": "user", "content": "build the thing"}},
             {"type": "ai-title", "aiTitle": "Thing builder", "sessionId": f.stem},
             {"type": "assistant", "message": {"content": [{"type": "text", "text": "Built it."}]}}]
    f.write_text("\n".join(json.dumps(l) for l in lines) + "\n" + "x" * 300)
    monkeypatch.setattr(importers, "CLAUDE_PROJECTS", tmp_path); monkeypatch.setattr(claude_sessions, "CLAUDE_PROJECTS", tmp_path)
    s = claude_sessions.list_sessions()
    assert s and s[0].title == "Thing builder" and s[0].project == "proj"
    text, last = claude_sessions.transcript_tail(s[0].path)
    assert "> build the thing" in text and last == "Built it."
    assert claude_sessions.find(f.stem).id == f.stem
