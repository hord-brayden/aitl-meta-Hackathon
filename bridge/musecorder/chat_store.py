"""Chats persist as JSON files in ~/.config/musecorder/chats/<id>.json. The device only ever sees a trimmed view."""
from __future__ import annotations
import json
import os
import secrets
import time
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any

from . import config


@dataclass
class Chat:
    id: str
    title: str = ""
    created: float = field(default_factory=time.time)
    updated: float = field(default_factory=time.time)
    model: str = ""
    messages: list[dict[str, Any]] = field(default_factory=list)   # {"role": "user"|"assistant", "content": str}
    ext: dict[str, Any] = field(default_factory=dict)              # provider state, e.g. {"muse_session": id}

    def transcript(self, limit: int = 3000) -> str:
        out = []
        for m in self.messages:
            out.append(("> " + m["content"] + "\n") if m["role"] == "user" else (m["content"] + "\n"))
        text = "\n".join(out)
        return text if len(text) <= limit else "…" + text[-limit:]

    def last_assistant(self) -> str:
        for m in reversed(self.messages):
            if m["role"] == "assistant":
                return m["content"]
        return ""


class ChatStore:
    def __init__(self, root: Path = config.CHATS_DIR):
        self.root = root
        self.root.mkdir(parents=True, exist_ok=True)

    def _path(self, cid: str) -> Path:
        if not cid.isalnum():
            raise ValueError("bad chat id")
        return self.root / f"{cid}.json"

    def new(self, model: str = "") -> Chat:
        return Chat(id=secrets.token_hex(4), model=model)

    def get(self, cid: str) -> Chat | None:
        p = self._path(cid)
        if not p.exists():
            return None
        return Chat(**json.loads(p.read_text()))

    def save(self, chat: Chat) -> None:
        chat.updated = time.time()
        p = self._path(chat.id)
        p.write_text(json.dumps(asdict(chat), ensure_ascii=False, indent=1))
        os.chmod(p, 0o600)

    def delete(self, cid: str) -> bool:
        p = self._path(cid)
        if p.exists():
            p.unlink()
            return True
        return False

    def list(self, limit: int = 30) -> list[dict[str, str]]:
        items = []
        for p in self.root.glob("*.json"):
            try:
                raw = json.loads(p.read_text())
                items.append((raw.get("updated", 0), {"id": raw["id"], "title": raw.get("title") or "(untitled)"}))
            except (ValueError, KeyError):
                continue
        items.sort(key=lambda t: t[0], reverse=True)
        return [i for _, i in items[:limit]]
