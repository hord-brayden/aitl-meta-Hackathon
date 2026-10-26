"""Bring existing conversations onto the device: plain text / markdown, or JSON [{role, content}]."""
from __future__ import annotations
import json
from pathlib import Path

from .chat_store import ChatStore, Chat

MAX_MSG = 4000          # chars per message kept (the device shows a trimmed view; the model gets the last 20 turns)
MAX_TURNS = 60


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
        chat.messages = [m for m in msgs if m["content"]][-MAX_TURNS:]
    chat.title = (title or (chat.messages[0]["content"].splitlines()[0] if chat.messages else path.stem))[:40]
    store.save(chat)
    return chat
