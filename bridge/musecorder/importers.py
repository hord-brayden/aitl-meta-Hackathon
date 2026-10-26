"""Bring existing conversations onto the device: Claude Code session logs, plain text, or JSON."""
from __future__ import annotations
import json
import re
from pathlib import Path

from .chat_store import ChatStore, Chat

CLAUDE_PROJECTS = Path.home() / ".claude" / "projects"
MAX_MSG = 4000          # chars per message kept (the device shows a trimmed view; the model gets the last 20 turns)
MAX_TURNS = 60


def _text_of(content) -> str:
    if isinstance(content, str):
        return content
    parts = []
    for b in content or []:
        if isinstance(b, dict) and b.get("type") == "text" and b.get("text"):
            parts.append(b["text"])
    return "\n".join(parts)


def claude_code_sessions(project_dir: str | None = None) -> list[Path]:
    """Most recent first. project_dir filters to one Claude Code project folder (its encoded path)."""
    root = CLAUDE_PROJECTS
    if not root.exists():
        return []
    files = []
    for p in root.glob("*/*.jsonl"):
        if project_dir and project_dir not in p.parent.name:
            continue
        files.append(p)
    return sorted(files, key=lambda p: p.stat().st_mtime, reverse=True)


def from_claude_code(path: Path, store: ChatStore, model: str = "", title: str = "") -> Chat:
    msgs: list[dict] = []
    for line in path.read_text(errors="replace").splitlines():
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = o.get("type")
        if kind not in ("user", "assistant"):
            continue
        m = o.get("message") or {}
        text = _text_of(m.get("content")).strip()
        if not text or text.startswith("<") and kind == "user" and "system-reminder" in text[:40]:
            continue
        text = re.sub(r"<system-reminder>.*?</system-reminder>", "", text, flags=re.S).strip()
        if not text:
            continue
        role = "user" if kind == "user" else "assistant"
        if msgs and msgs[-1]["role"] == role:           # merge consecutive same-role pieces
            msgs[-1]["content"] = (msgs[-1]["content"] + "\n\n" + text)[:MAX_MSG]
        else:
            msgs.append({"role": role, "content": text[:MAX_MSG]})
    if msgs and msgs[0]["role"] == "assistant":
        msgs.insert(0, {"role": "user", "content": "(session start)"})
    if msgs and msgs[-1]["role"] == "user":
        msgs.pop()                                       # keep user/assistant alternation ending on assistant
    msgs = msgs[-MAX_TURNS:]
    if msgs and msgs[0]["role"] == "assistant":
        msgs = msgs[1:]
    chat = store.new(model)
    chat.title = (title or (msgs[0]["content"].splitlines()[0] if msgs else path.stem))[:40]
    chat.messages = msgs
    store.save(chat)
    return chat


def from_text(path: Path, store: ChatStore, model: str = "", title: str = "") -> Chat:
    """Plain text/markdown: lines starting with '> ' are the user, everything else the assistant. JSON: [{role,content}]."""
    chat = store.new(model)
    raw = path.read_text(errors="replace")
    if path.suffix.lower() == ".json":
        chat.messages = [{"role": m["role"], "content": str(m["content"])[:MAX_MSG]} for m in json.loads(raw) if m.get("role") in ("user", "assistant")]
    else:
        msgs, cur_role, buf = [], None, []
        for line in raw.splitlines():
            role = "user" if line.startswith("> ") else "assistant"
            if role != cur_role and buf:
                msgs.append({"role": cur_role, "content": "\n".join(buf).strip()[:MAX_MSG]}); buf = []
            cur_role = role
            buf.append(line[2:] if role == "user" else line)
        if buf:
            msgs.append({"role": cur_role, "content": "\n".join(buf).strip()[:MAX_MSG]})
        chat.messages = [m for m in msgs if m["content"]]
    chat.title = (title or (chat.messages[0]["content"].splitlines()[0] if chat.messages else path.stem))[:40]
    store.save(chat)
    return chat
