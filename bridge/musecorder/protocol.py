"""NDJSON frames shared with the firmware (firmware/src/core/app.cpp). One JSON object per line."""
from __future__ import annotations
import json
from typing import Any, Iterator

MAX_FRAME = 6000          # must stay under LINK_MAX_MSG on the device (6144) incl. JSON overhead
DELTA_CHUNK = 160         # characters of streamed text per chat.delta frame
TEXT_CHUNK = 3000         # characters per oversized text field (chat.view etc.)


def encode(msg: dict[str, Any]) -> bytes:
    data = json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"
    return data.encode("utf-8")


def decode(line: bytes | str) -> dict[str, Any] | None:
    if isinstance(line, bytes):
        line = line.decode("utf-8", "replace")
    line = line.strip()
    if not line.startswith("{"):
        return None               # device log lines start with '#'
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    return obj if isinstance(obj, dict) and "t" in obj else None


def chunks(text: str, size: int = DELTA_CHUNK) -> Iterator[str]:
    for i in range(0, len(text), size):
        yield text[i:i + size]


def fits(msg: dict[str, Any]) -> bool:
    return len(encode(msg)) <= MAX_FRAME


def truncate_for_view(text: str, limit: int = TEXT_CHUNK) -> str:
    if len(text) <= limit:
        return text
    cut = text[-limit:]
    nl = cut.find("\n")
    return ("…" + cut[nl + 1:]) if 0 <= nl < 200 else "…" + cut
