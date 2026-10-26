"""API keys: environment variables first, then the macOS Keychain (via /usr/bin/security).

Nothing here is ever sent to the device. `musecorder keys set anthropic` stores a key in the login keychain
under service "musecorder.anthropic". Rotate a key in the provider console if it was ever pasted anywhere else.
"""
from __future__ import annotations
import os
import subprocess
import sys

ENV_NAMES = {
    "anthropic": ["ANTHROPIC_API_KEY"],
    "gemini": ["GEMINI_API_KEY", "GOOGLE_API_KEY"],
    "openai": ["OPENAI_API_KEY"],
    "openrouter": ["OPENROUTER_API_KEY"],
    "muse": ["MUSE_API_KEY"],
}
ACCOUNT = "aitl"


def _service(provider: str) -> str:
    return f"musecorder.{provider}"


def get(provider: str, key_env: str | None = None) -> str | None:
    names = list(ENV_NAMES.get(provider, []))
    if key_env and key_env not in names:
        names.insert(0, key_env)
    for n in names:
        v = os.environ.get(n)
        if v:
            return v.strip()
    if sys.platform != "darwin":
        return None
    try:
        out = subprocess.run(
            ["/usr/bin/security", "find-generic-password", "-a", ACCOUNT, "-s", _service(provider), "-w"],
            capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    return out.stdout.strip() or None if out.returncode == 0 else None


def set_key(provider: str, value: str) -> bool:
    if sys.platform != "darwin":
        raise RuntimeError("Keychain storage is macOS-only; export the environment variable instead.")
    out = subprocess.run(
        ["/usr/bin/security", "add-generic-password", "-a", ACCOUNT, "-s", _service(provider), "-w", value, "-U",
         "-l", f"AITL bridge key: {provider}"],
        capture_output=True, text=True, timeout=10,
    )
    return out.returncode == 0


def delete(provider: str) -> bool:
    out = subprocess.run(["/usr/bin/security", "delete-generic-password", "-a", ACCOUNT, "-s", _service(provider)],
                         capture_output=True, text=True, timeout=10)
    return out.returncode == 0


def source(provider: str, key_env: str | None = None) -> str:
    """Where the key comes from, for `musecorder doctor` (never the value)."""
    names = list(ENV_NAMES.get(provider, []))
    if key_env and key_env not in names:
        names.insert(0, key_env)
    for n in names:
        if os.environ.get(n):
            return f"env:{n}"
    if get(provider):
        return "keychain"
    return "missing"
