from __future__ import annotations
from dataclasses import dataclass
from typing import AsyncIterator


class ProviderError(RuntimeError):
    pass


@dataclass
class ModelInfo:
    id: str          # what the device selects, e.g. "claude-opus-5-5" or "ollama/gemma3n:e4b"
    label: str
    provider: str    # display group
    ok: bool         # credentials present / endpoint reachable


class Provider:
    name = "base"
    label = "Base"

    def available(self) -> bool:
        return False

    def owns(self, model_id: str) -> bool:
        return False

    async def models(self) -> list[ModelInfo]:
        return []

    async def stream(self, model_id: str, system: str, messages: list[dict], max_tokens: int) -> AsyncIterator[str]:
        """Yield text deltas. messages: [{"role": "user"|"assistant", "content": str}, ...]"""
        raise ProviderError("not implemented")
        yield ""  # pragma: no cover
