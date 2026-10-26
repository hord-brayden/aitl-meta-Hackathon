"""Any OpenAI-compatible chat endpoint (OpenAI, OpenRouter, Meta Muse Spark when an endpoint is configured, LM Studio...)."""
from __future__ import annotations
from typing import AsyncIterator

from .base import Provider, ModelInfo, ProviderError
from .. import secrets


class OpenAICompatProvider(Provider):
    def __init__(self, entry: dict):
        self.name = entry["name"]
        self.label = entry.get("label") or entry["name"]
        self.base_url = (entry.get("base_url") or "").rstrip("/")
        self.key_env = entry.get("key_env")
        self.model_ids = list(entry.get("models") or [])
        self.prefix = f"{self.name}/"
        self._client = None

    def _key(self) -> str | None:
        return secrets.get(self.name, self.key_env)

    def available(self) -> bool:
        return bool(self.base_url) and (bool(self._key()) or self.base_url.startswith("http://127.0.0.1") or self.base_url.startswith("http://localhost"))

    def owns(self, model_id: str) -> bool:
        return model_id.startswith(self.prefix)

    async def models(self) -> list[ModelInfo]:
        ok = self.available()
        return [ModelInfo(self.prefix + m, m, self.label, ok) for m in self.model_ids]

    def _get_client(self):
        if self._client is None:
            from openai import AsyncOpenAI
            self._client = AsyncOpenAI(base_url=self.base_url, api_key=self._key() or "local")
        return self._client

    async def stream(self, model_id: str, system: str, messages: list[dict], max_tokens: int) -> AsyncIterator[str]:
        if not self.base_url:
            raise ProviderError(f"{self.label}: no base_url configured in config.toml")
        if not self.available():
            raise ProviderError(f"{self.label}: key missing (musecorder keys set {self.name})")
        model = model_id[len(self.prefix):]
        client = self._get_client()
        try:
            stream = await client.chat.completions.create(
                model=model, stream=True, max_completion_tokens=max_tokens,
                messages=[{"role": "system", "content": system}] + messages)
            async for chunk in stream:
                if chunk.choices and chunk.choices[0].delta and chunk.choices[0].delta.content:
                    yield chunk.choices[0].delta.content
        except Exception as e:
            raise ProviderError(f"{self.label}: {str(e)[:160]}") from e
