"""Builds the model catalog the device shows and routes a model id to its provider."""
from __future__ import annotations
import asyncio

from .providers import Provider, ModelInfo, ProviderError
from .providers.claude_code_provider import ClaudeCodeProvider
from .providers.anthropic_provider import AnthropicProvider
from .providers.gemini_provider import GeminiProvider
from .providers.ollama_provider import OllamaProvider
from .providers.openai_compat import OpenAICompatProvider


class Router:
    def __init__(self, cfg: dict):
        p = cfg["providers"]
        self.providers: list[Provider] = []
        if p.get("claude_code", {}).get("enabled", True):
            self.providers.append(ClaudeCodeProvider(p.get("claude_code", {})))
        if p.get("anthropic", {}).get("enabled", True):
            self.providers.append(AnthropicProvider(p["anthropic"]))
        if p.get("gemini", {}).get("enabled", True):
            self.providers.append(GeminiProvider(p["gemini"]))
        if p.get("ollama", {}).get("enabled", True):
            self.providers.append(OllamaProvider(p["ollama"]))
        for entry in p.get("openai_compatible", []):
            if entry.get("enabled", True):
                self.providers.append(OpenAICompatProvider(entry))
        self._catalog: list[ModelInfo] = []
        self.default_model = cfg["bridge"].get("default_model") or "claude-code/default"

    async def refresh(self) -> list[ModelInfo]:
        results = await asyncio.gather(*(pr.models() for pr in self.providers), return_exceptions=True)
        cat: list[ModelInfo] = []
        for r in results:
            if isinstance(r, list):
                cat.extend(r)
        self._catalog = cat
        return cat

    @property
    def catalog(self) -> list[ModelInfo]:
        return self._catalog

    def resolve(self, model_id: str) -> Provider:
        for pr in self.providers:
            if pr.owns(model_id):
                return pr
        raise ProviderError(f"No provider for model '{model_id}'")

    def pick_default(self) -> str:
        ok = [m.id for m in self._catalog if m.ok]
        if self.default_model in ok:
            return self.default_model
        return ok[0] if ok else self.default_model

    def as_wire(self) -> list[dict]:
        return [{"id": m.id, "label": m.label, "provider": m.provider, "ok": m.ok} for m in self._catalog]
