"""Configuration: ~/.config/musecorder/config.toml (+ chats/, devices.json, tls/).

Secrets (API keys, device tokens) never live in this file; see secrets.py / security.py.
"""
from __future__ import annotations
import os
import sys
from pathlib import Path
from typing import Any

if sys.version_info >= (3, 11):
    import tomllib
else:  # pragma: no cover
    import tomli as tomllib
import tomli_w

CONFIG_DIR = Path(os.environ.get("MUSECORDER_HOME", Path.home() / ".config" / "musecorder"))
CONFIG_FILE = CONFIG_DIR / "config.toml"
CHATS_DIR = CONFIG_DIR / "chats"
TLS_DIR = CONFIG_DIR / "tls"
DEVICES_FILE = CONFIG_DIR / "devices.json"

MUSE_API_BASE = "https://api.meta.ai/v1"
MUSE_MODELS = ["muse-spark-1.3", "muse-spark-1.2"]
MUSE_VOICE_MODEL = "muse-voice-transcribe-1.0"

DEFAULT_CONFIG: dict[str, Any] = {
    "bridge": {
        "name": "",                 # defaults to the Mac's hostname
        "serial_port": "auto",      # "auto" | "/dev/cu.usbserial-110" | "off"
        "serial_baud": 460800,
        "wifi": False,              # listen for Wi-Fi devices (TLS WebSocket)
        "ws_port": 7777,
        "wifi_mode": "signed",      # "signed" (HMAC frames; fits a classic ESP32) | "tls" (pinned cert; needs PSRAM)
        "ble": False,               # link over Bluetooth (Nordic UART) too; needs `pip install bleak`
        "bind": "0.0.0.0",
        "discovery": True,          # advertise the bridge via mDNS when zeroconf is installed
        "default_model": "muse-code/default",     # Muse Code subscription (muse login): no per-token API spend. "muse/muse-spark-1.3" uses the Model API key instead
        "system_prompt": (
            "You are Musecorder, a hardware AI companion running on a small ESP32 touchscreen device. "
            "You are shown on a 2.8-inch screen, so answer tightly: lead with the answer, plain text, "
            "no markdown tables or headers, short paragraphs. Expand only when asked."
        ),
        "max_output_tokens": 2048,
        "title_model": "",          # model used to title chats; empty = same as the chat's model
    },
    "stt": {
        "engine": "auto",           # auto | mlx-whisper | muse-cli | muse-api | none
        "whisper_model": "mlx-community/whisper-base-mlx",
        "muse_bin": "muse",         # `muse voice transcribe` (needs `muse login`)
        "language": "en",
    },
    "hid": {
        "typing_method": "paste",   # "paste" (clipboard + Cmd-V, Unicode-safe) | "keystroke"
        "restore_clipboard": True,
        "dictation": "menu",        # "menu" (Edit > Start Dictation) | "shortcut"
        "dictation_shortcut": "ctrl ctrl",  # fallback when the app has no Edit > Start Dictation: "ctrl ctrl" | "rcmd rcmd" | "f5"
        "notify": True,             # macOS notification on first HID action per session
    },
    "applets": {
        "http_timeout": 15,         # hard cap for net.http (seconds)
        "http_max_body": 2000,      # characters of response body relayed to the device
        "http_rate": 2,             # net.http requests per second per device
        "ai_max_tokens": 512,       # ai.ask answers are short: they land on a 2.8-inch screen
    },
    "providers": {
        "muse_code": {
            "enabled": True,
            "muse_bin": "",             # empty = `muse` on PATH or ~/.local/bin/muse
            "models": ["default"],      # "default" = Muse Code's configured model; add ids to offer more
            "provider": "",             # "" = Muse Code's configured provider; "echo" for offline testing
            "workspace": "",            # directory device chats run in; empty = ~/.config/musecorder/muse-workspace
            "permission_profile": "",   # e.g. ":ask-me"; empty = host default (approvals come to the device)
            "approval_wait": 75,        # seconds to wait for Allow/Deny on the device before denying
            "reasoning_effort": "",     # none|minimal|low|medium|high|xhigh|max|ultra; empty = host default
        },
        "ollama": {"enabled": True, "base_url": "http://127.0.0.1:11434", "models": [], "live_list": True},
        "openai_compatible": [
            {"name": "muse", "label": "Meta Muse", "base_url": MUSE_API_BASE, "key_env": "META_API_KEY", "models": list(MUSE_MODELS)},
            {"name": "openai", "label": "OpenAI", "base_url": "https://api.openai.com/v1", "key_env": "OPENAI_API_KEY", "models": ["gpt-5", "gpt-5-mini"]},
            {"name": "openrouter", "label": "OpenRouter", "base_url": "https://openrouter.ai/api/v1", "key_env": "OPENROUTER_API_KEY", "models": ["meta-llama/llama-4-maverick", "google/gemma-3-27b-it"]},
        ],
    },
    "actions": [
        {"id": "ask_voice", "label": "Ask by voice", "kind": "ask_voice"},
        {"id": "summarize_clip", "label": "Summarize clipboard", "kind": "clipboard", "prompt": "Summarize this in 3 short bullet points:\n\n{clip}"},
        {"id": "fix_clip", "label": "Fix grammar (clipboard -> type)", "kind": "clipboard_type", "prompt": "Fix spelling, grammar and punctuation. Keep meaning, tone and length. Return only the corrected text:\n\n{clip}"},
        {"id": "explain_clip", "label": "Explain clipboard", "kind": "clipboard", "prompt": "Explain this clearly for a smart non-expert, in under 120 words:\n\n{clip}"},
        {"id": "reply_clip", "label": "Draft a reply (-> type)", "kind": "clipboard_type", "prompt": "Draft a concise, friendly reply to this message. Return only the reply:\n\n{clip}"},
        {"id": "translate_clip", "label": "Translate clipboard -> EN", "kind": "clipboard", "prompt": "Translate to English. Return only the translation:\n\n{clip}"},
        {"id": "todo_clip", "label": "Extract action items", "kind": "clipboard", "prompt": "Extract the action items as a short checklist:\n\n{clip}"},
    ],
}


def _deep_merge(base: dict, over: dict) -> dict:
    out = dict(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


def ensure_dirs() -> None:
    for d in (CONFIG_DIR, CHATS_DIR, TLS_DIR):
        d.mkdir(parents=True, exist_ok=True)
        try:
            os.chmod(d, 0o700)
        except OSError:
            pass


def load_dotenv() -> list[str]:
    """Load KEY=value lines from .env files into the environment (never overriding what is already set). Looked up in
    the repo root (two levels above this package), the config dir and the current directory. Returns the names loaded."""
    loaded: list[str] = []
    candidates = [Path(__file__).resolve().parents[2] / ".env", CONFIG_DIR / ".env", Path.cwd() / ".env"]
    for f in candidates:
        try:
            if not f.is_file():
                continue
            for line in f.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                if line.startswith("export "):
                    line = line[7:].strip()
                k, _, v = line.partition("=")
                k = k.strip(); v = v.strip().strip('"').strip("'")
                if k and v and k not in os.environ:
                    os.environ[k] = v; loaded.append(k)
        except OSError:
            continue
    return loaded


def load() -> dict[str, Any]:
    ensure_dirs()
    load_dotenv()
    if not CONFIG_FILE.exists():
        save(DEFAULT_CONFIG)
        return _deep_merge({}, DEFAULT_CONFIG)
    with CONFIG_FILE.open("rb") as f:
        user = tomllib.load(f)
    cfg = _deep_merge(DEFAULT_CONFIG, user)
    if user.get("actions"):
        cfg["actions"] = user["actions"]  # lists replace, not merge
    if user.get("providers", {}).get("openai_compatible"):
        cfg["providers"]["openai_compatible"] = user["providers"]["openai_compatible"]
    return cfg


def save(cfg: dict[str, Any]) -> None:
    ensure_dirs()
    with CONFIG_FILE.open("wb") as f:
        tomli_w.dump(cfg, f)
    os.chmod(CONFIG_FILE, 0o600)


def bridge_name(cfg: dict[str, Any]) -> str:
    import socket
    return cfg["bridge"].get("name") or socket.gethostname().split(".")[0]
