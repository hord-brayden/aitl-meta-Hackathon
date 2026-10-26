"""Meta Muse through the Meta Model API (https://api.meta.ai/v1, OpenAI-compatible chat completions).

Key: `META_API_KEY` (or `MODEL_API_KEY`, the name dev.meta.ai uses in its examples) or the Keychain entry written by
`musecorder keys set muse`. Model ids on the device: `muse/muse-spark-1.3`, `muse/muse-spark-1.2`.
"""
from __future__ import annotations

from .openai_compat import OpenAICompatProvider
from .. import config

MUSE_DEFAULTS = {"name": "muse", "label": "Meta Muse", "base_url": config.MUSE_API_BASE,
                 "key_env": "META_API_KEY", "models": list(config.MUSE_MODELS)}


class MuseProvider(OpenAICompatProvider):
    """The `muse` openai_compatible entry with the Meta defaults filled in (config.toml may override any field)."""

    def __init__(self, entry: dict | None = None):
        merged = dict(MUSE_DEFAULTS)
        for k, v in (entry or {}).items():
            if v not in ("", None, []):
                merged[k] = v
        super().__init__(merged)
