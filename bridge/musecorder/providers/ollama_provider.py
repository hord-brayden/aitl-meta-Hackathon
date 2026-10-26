"""Local models through Ollama's native API (Gemma, Llama, Qwen, ...). Nothing leaves the machine."""
from __future__ import annotations
import json
from typing import AsyncIterator

import httpx

from .base import Provider, ModelInfo, ProviderError

PREFIX = "ollama/"


class OllamaProvider(Provider):
    name = "ollama"
    label = "Ollama (local)"

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.base = (cfg.get("base_url") or "http://127.0.0.1:11434").rstrip("/")
        self._reachable: bool | None = None

    def available(self) -> bool:
        return self._reachable is not False

    def owns(self, model_id: str) -> bool:
        return model_id.startswith(PREFIX)

    async def models(self) -> list[ModelInfo]:
        ids = list(self.cfg.get("models") or [])
        try:
            async with httpx.AsyncClient(timeout=2.5) as c:
                r = await c.get(f"{self.base}/api/tags")
                r.raise_for_status()
                live = [m["name"] for m in r.json().get("models", [])]
                self._reachable = True
                if self.cfg.get("live_list", True):
                    ids = [i for i in ids if i in live] + [i for i in live if i not in ids]
        except Exception:
            self._reachable = False
        return [ModelInfo(PREFIX + i, i, self.label, self._reachable is True) for i in ids]

    async def stream(self, model_id: str, system: str, messages: list[dict], max_tokens: int) -> AsyncIterator[str]:
        model = model_id[len(PREFIX):]
        body = {"model": model, "stream": True, "options": {"num_predict": max_tokens},
                "messages": [{"role": "system", "content": system}] + messages}
        try:
            async with httpx.AsyncClient(timeout=httpx.Timeout(10, read=300)) as c:
                async with c.stream("POST", f"{self.base}/api/chat", json=body) as r:
                    if r.status_code != 200:
                        text = (await r.aread()).decode("utf-8", "replace")[:200]
                        raise ProviderError(f"Ollama {r.status_code}: {text}")
                    async for line in r.aiter_lines():
                        if not line:
                            continue
                        obj = json.loads(line)
                        if "error" in obj:
                            raise ProviderError(f"Ollama: {obj['error']}")
                        piece = obj.get("message", {}).get("content", "")
                        if piece:
                            yield piece
                        if obj.get("done"):
                            break
        except httpx.HTTPError as e:
            raise ProviderError(f"Ollama unreachable at {self.base} (is `ollama serve` running?)") from e
