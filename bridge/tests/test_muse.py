"""Muse Code provider helpers (no `muse` process) and the on-disk session scan."""
import json
from pathlib import Path

from musecorder import muse_sessions, muse_setup
from musecorder.providers.muse_code_provider import parse_exec_line, pick_choice, MuseCodeProvider, PREFIX
from musecorder.providers.muse_provider import MuseProvider


def test_parse_exec_lines():
    delta = {"payload_type": "run.output.delta", "payload": {"text": "echo: hi"}}
    done = {"payload_type": "run.terminal.completed", "payload": {"terminal": "completed", "text": "echo: hi"}}
    failed = {"payload_type": "run.terminal.failed", "payload": {"terminal": "failed", "reason": "no provider"}}
    assert parse_exec_line(json.dumps(delta)) == ("delta", "echo: hi")
    assert parse_exec_line(json.dumps(done)) == ("done", "echo: hi")
    assert parse_exec_line(json.dumps(failed)) == ("error", "no provider")
    assert parse_exec_line("muse: workspace root: /x") is None
    assert parse_exec_line("{broken") is None
    assert parse_exec_line(json.dumps({"payload_type": "task.lifecycle.started"})) is None


def test_pick_choice_maps_device_answer_onto_server_choices():
    choices = [{"choiceId": "allow-once", "decision": "approved", "scope": "once", "label": "Allow once"},
               {"choiceId": "allow-session", "decision": "approvedForSession", "scope": "session", "label": "Allow for session"},
               {"choiceId": "reject", "decision": "denied", "scope": "once", "label": "Deny", "acceptsFeedback": True}]
    assert pick_choice(choices, "allow")["choiceId"] == "allow-once"
    assert pick_choice(choices, "deny")["choiceId"] == "reject"
    assert pick_choice(choices, None)["choiceId"] == "reject"          # no answer -> deny, never allow
    assert pick_choice(choices[1:], "allow")["choiceId"] == "allow-session"
    assert pick_choice([], "allow") is None


async def test_provider_catalog_and_prefix(monkeypatch):
    p = MuseCodeProvider({"models": ["default", "muse-spark-1.3"]})
    monkeypatch.setattr(p, "muse_bin", lambda: "/nonexistent/muse")
    models = await p.models()
    assert [m.id for m in models] == [PREFIX + "default", PREFIX + "muse-spark-1.3"] and models[0].ok
    assert p.owns("muse-code/default") and not p.owns("muse/muse-spark-1.3")
    monkeypatch.setattr(p, "muse_bin", lambda: None)
    assert not p.available()


def test_muse_provider_defaults(monkeypatch):
    monkeypatch.delenv("META_API_KEY", raising=False); monkeypatch.delenv("MODEL_API_KEY", raising=False)
    p = MuseProvider({"name": "muse", "base_url": "", "models": []})
    assert p.base_url == "https://api.meta.ai/v1" and p.model_ids == ["muse-spark-1.3", "muse-spark-1.2"] and p.key_env == "META_API_KEY"
    assert p.owns("muse/muse-spark-1.3") and p.label == "Meta Muse"
    monkeypatch.setenv("META_API_KEY", "x")
    assert p.available()


def _session_log(sid: str, prompt: str, root: str) -> str:
    inner = {"stream": {"kind": "session", "id": sid}, "payload_type": "runtime.session_start.intake_v3",
             "payload": {"workspace_root": root, "prompt": prompt}}
    return json.dumps({"retained_frame": "x", "children": [{"record_json": json.dumps(inner)}]}) + "\n"


def test_scan_disk(tmp_path: Path):
    sid1 = "01a178de-7011-7262-a0d7-973da6928b82"; sid2 = "01a178de-7011-7262-a0d7-973da6928b83"
    d1 = tmp_path / "2026" / "10" / "26" / sid1; d1.mkdir(parents=True)
    (d1 / "session.jsonl").write_text(_session_log(sid1, "build the thing", "/Users/me/proj"))
    d2 = tmp_path / "2026" / "10" / "25" / sid2; d2.mkdir(parents=True)
    (d2 / "session.jsonl").write_text("{}\n")
    sub = d1 / "subagent" / sid2; sub.mkdir(parents=True); (sub / "session.jsonl").write_text("{}\n")
    (tmp_path / "2026" / "10" / "26" / "not-a-uuid").mkdir(); (tmp_path / "2026" / "10" / "26" / "not-a-uuid" / "session.jsonl").write_text("{}")
    rows = muse_sessions.scan_disk(root=tmp_path)
    assert [r.id for r in rows] == [sid1, sid2] or {r.id for r in rows} == {sid1, sid2}
    info = muse_sessions.find_disk(sid1, root=tmp_path)
    assert info and info.title == "build the thing" and info.project == "proj"
    assert info.wire()["id"] == sid1 and info.wire()["age"].endswith("m")
    assert muse_sessions.find_disk("nope", root=tmp_path) is None
    assert muse_sessions.find_disk(sid2, root=tmp_path).title == "(no prompt yet)"


def test_from_wire_and_transcript():
    info = muse_sessions.from_wire({"sessionId": "abc", "title": "Fix the bug", "workspaceRoot": "/x/y/repo", "updatedAt": "2026-10-26T15:40:06.79924Z"})
    assert info.title == "Fix the bug" and info.project == "repo" and info.updated > 0
    text, last = muse_sessions.transcript_from_read({"items": [
        {"kind": "userMessage", "text": "hi"}, {"kind": "agentMessage", "text": "thinking", "messagePhase": "commentary"},
        {"kind": "agentMessage", "text": "hello"}, {"kind": "toolCall", "text": "x"}]})
    assert text == "> hi\n\nhello\n" and last == "hello"


def test_muse_setup_settings(tmp_path: Path):
    settings = tmp_path / "muse" / "settings.json"
    out = muse_setup.write_settings("/venv/bin/musecorder-mcp", settings)
    cfg = json.loads(settings.read_text())
    assert cfg["schema_version"] == 1 and cfg["mcpServers"]["musecorder"] == {"transport": "stdio", "command": "/venv/bin/musecorder-mcp", "args": []}
    assert any("added" in o for o in out)
    settings.write_text(json.dumps({"schema_version": 1, "model": "muse-spark-1.3", "mcpServers": {"other": {"transport": "stdio", "command": "x"}}}))
    out = muse_setup.write_settings("/new/musecorder-mcp", settings)
    cfg = json.loads(settings.read_text())
    assert cfg["model"] == "muse-spark-1.3" and set(cfg["mcpServers"]) == {"other", "musecorder"}
    assert cfg["mcpServers"]["musecorder"]["command"] == "/new/musecorder-mcp"
    assert list(settings.parent.glob("settings.json.bak-*")) and any("backed up" in o for o in out)


def test_install_skill(tmp_path: Path):
    out = muse_setup.install_skill(tmp_path)
    dest = tmp_path / "musecorder"
    assert (dest / "SKILL.md").exists() and (dest / "reference" / "applets.md").exists()
    assert len(list((dest / "examples" / "applets").glob("*.json"))) == 4 and "installed skill" in out[0]
