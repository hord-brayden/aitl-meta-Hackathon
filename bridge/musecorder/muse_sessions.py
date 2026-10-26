"""Browse Muse Code sessions for the device's session picker (`cc.list` / `cc.open`).

Preferred source: `session/list` through the running `muse serve` host (titles, workspace roots, timestamps).
Fallback: the on-disk store ~/.local/share/muse/sessions/YYYY/MM/DD/<uuid>/session.jsonl (ids, mtimes, a best-effort
first prompt). Session logs are nested JSON-in-JSON; we only sniff them with regexes, never execute anything from them.
"""
from __future__ import annotations
import datetime as dt
import logging
import os
import re
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

log = logging.getLogger("musecorder.musesessions")
SESSIONS_DIR = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "muse" / "sessions"
MAX_SNIFF = 256 * 1024
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
_PROMPT_RE = re.compile(r'\\"prompt\\":\\"((?:[^"\\]|\\\\.){1,80})')
_ROOT_RE = re.compile(r'\\"workspace_?[rR]oot\\":\\"((?:[^"\\]|\\\\.){1,300})')


@dataclass
class SessionInfo:
    id: str
    title: str
    project: str
    updated: float
    path: Path | None = None

    def age(self) -> str:
        d = max(0, time.time() - self.updated)
        if d < 3600: return f"{int(d // 60)}m"
        if d < 86400: return f"{int(d // 3600)}h"
        return f"{int(d // 86400)}d"

    def wire(self) -> dict[str, str]:
        return {"id": self.id, "title": self.title[:40], "project": self.project[:24], "age": self.age()}


def _unescape(s: str) -> str:
    return s.replace('\\\\"', '"').replace("\\\\n", " ").replace("\\\\\\\\", "\\")


def _parse_rfc3339(s: str | None) -> float:
    if not s:
        return 0.0
    try:
        return dt.datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return 0.0


def from_wire(s: dict[str, Any]) -> SessionInfo:
    """A `Session` object from `session/list` -> SessionInfo."""
    title = s.get("title") or s.get("generatedName") or s.get("name") or s.get("firstUserPrompt") or "(untitled)"
    root = s.get("workspaceRoot") or ""
    return SessionInfo(id=str(s.get("sessionId", ""))[:36], title=str(title).splitlines()[0][:60],
                       project=Path(root).name if root else "", updated=_parse_rfc3339(s.get("updatedAt") or s.get("lastActivityAt")),
                       path=Path(s["path"]) if s.get("path") else None)


def describe_disk(path: Path) -> SessionInfo | None:
    sid = path.parent.name
    if not UUID_RE.match(sid):
        return None
    try:
        with path.open("rb") as f:
            head = f.read(MAX_SNIFF).decode("utf-8", "replace")
        updated = path.stat().st_mtime
    except OSError:
        return None
    m = _PROMPT_RE.search(head)
    title = _unescape(m.group(1)).strip() if m else ""
    r = _ROOT_RE.search(head)
    root = _unescape(r.group(1)) if r else ""
    return SessionInfo(id=sid, title=title or "(no prompt yet)", project=Path(root).name if root else path.parents[1].name, updated=updated, path=path)


def scan_disk(limit: int = 20, root: Path | None = None) -> list[SessionInfo]:
    base = root or SESSIONS_DIR
    if not base.exists():
        return []
    files = [p for p in base.glob("[0-9]*/*/*/*/session.jsonl") if "subagent" not in p.parts]
    files.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    out: list[SessionInfo] = []
    for p in files:
        info = describe_disk(p)
        if info:
            out.append(info)
        if len(out) >= limit:
            break
    return out


def find_disk(session_id: str, root: Path | None = None) -> SessionInfo | None:
    base = root or SESSIONS_DIR
    if not UUID_RE.match(session_id) or not base.exists():
        return None
    for p in base.glob(f"[0-9]*/*/*/{session_id}/session.jsonl"):
        return describe_disk(p)
    return None


async def list_sessions(provider, limit: int = 15) -> list[SessionInfo]:
    """Live listing through the Muse Code provider when possible, else the on-disk scan."""
    if provider is not None:
        try:
            rows = await provider.list_sessions(limit)
            if rows:
                return [from_wire(r) for r in rows][:limit]
        except Exception as e:  # noqa: BLE001
            log.info("session/list unavailable (%s); scanning disk", str(e)[:120])
    return scan_disk(limit)


async def find(provider, session_id: str) -> SessionInfo | None:
    if provider is not None:
        try:
            res = await provider.read_session(session_id)
            if res.get("session"):
                return from_wire(res["session"])
        except Exception as e:  # noqa: BLE001
            log.debug("session/read failed: %s", e)
    return find_disk(session_id)


def transcript_from_read(res: dict[str, Any], limit_chars: int = 2800) -> tuple[str, str]:
    """(trimmed transcript, last agent text) from a `session/read` result's items, if it carried any."""
    turns: list[tuple[str, str]] = []
    for it in res.get("items") or []:
        kind = it.get("kind")
        text = str(it.get("text") or "").strip()
        if kind == "userMessage" and text:
            turns.append(("user", text))
        elif kind == "agentMessage" and text and it.get("messagePhase") != "commentary":
            turns.append(("assistant", text))
    text = "\n".join((("> " + t) if k == "user" else t) + "\n" for k, t in turns)
    last = next((t for k, t in reversed(turns) if k == "assistant"), "")
    if len(text) > limit_chars:
        text = "…" + text[-limit_chars:]
    return text, last[:1500]
