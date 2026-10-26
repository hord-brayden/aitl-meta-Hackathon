"""Signed-frame Wi-Fi link for RAM-limited devices.

A classic ESP32 cannot hold a TLS session next to the UI, so the Wi-Fi link runs over plain WebSocket with every
frame authenticated: `{"n": counter, "m": "<json string>", "s": hex(HMAC-SHA256(key, f"{sid}.{n}.{m}"))}`.
  key = SHA-256(device token)        the bridge already stores exactly this hash; the device derives it from its token
  sid = random per-session nonce      issued by the bridge in its first (signed) frame; replaying a captured session fails
  n   = strictly increasing counter   per direction, per session
Integrity and authenticity hold; confidentiality is whatever the LAN provides. Use TLS mode on boards with PSRAM.
"""
from __future__ import annotations
import hashlib
import hmac
import json
import secrets
from typing import Any


def key_from_token_hash(token_hash_hex: str) -> bytes:
    return bytes.fromhex(token_hash_hex)


def key_from_token(token: str) -> bytes:
    return hashlib.sha256(token.encode()).digest()


def sign(key: bytes, sid: str, n: int, m: str) -> str:
    return hmac.new(key, f"{sid}.{n}.{m}".encode("utf-8"), hashlib.sha256).hexdigest()


class Signer:
    """Per-session state for one direction pair."""
    def __init__(self, key: bytes, sid: str | None = None):
        self.key = key
        self.sid = sid or secrets.token_hex(8)
        self.tx = secrets.randbelow(1 << 30)
        self.rx_last = -1

    def wrap(self, msg: dict[str, Any]) -> str:
        self.tx += 1
        m = json.dumps(msg, ensure_ascii=False, separators=(",", ":"))
        return json.dumps({"n": self.tx, "sid": self.sid, "m": m, "s": sign(self.key, self.sid, self.tx, m)}, ensure_ascii=False, separators=(",", ":"))

    def unwrap(self, raw: str, allow_no_sid: bool = False) -> dict[str, Any] | None:
        try:
            env = json.loads(raw)
            n, m, s = int(env["n"]), str(env["m"]), str(env["s"])
        except (ValueError, KeyError, TypeError):
            return None
        sid = str(env.get("sid", "")) if not allow_no_sid else str(env.get("sid", self.sid) or "")
        if not allow_no_sid and sid != self.sid:
            return None
        if n <= self.rx_last:
            return None                                  # replay / reordering
        if not hmac.compare_digest(sign(self.key, sid, n, m), s):
            return None
        self.rx_last = n
        try:
            obj = json.loads(m)
        except ValueError:
            return None
        return obj if isinstance(obj, dict) and "t" in obj else None
