"""Speech-to-text engines for device-microphone dictation. Picked by config `stt.engine`:
  auto         -> mlx-whisper if installed (local, free, Apple Silicon), else gemini (needs GEMINI_API_KEY)
  mlx-whisper  -> pip install mlx-whisper   (model downloads on first use; see stt.whisper_model)
  gemini       -> google-genai with inline audio
"""
from __future__ import annotations
import asyncio
import logging
import tempfile
from pathlib import Path

from . import secrets

log = logging.getLogger("musecorder.stt")


class SttError(RuntimeError):
    pass


def available_engines() -> list[str]:
    out = []
    try:
        import mlx_whisper  # noqa: F401
        out.append("mlx-whisper")
    except ImportError:
        pass
    if secrets.get("gemini"):
        out.append("gemini")
    return out


def pick(cfg: dict) -> str:
    want = (cfg.get("engine") or "auto").lower()
    avail = available_engines()
    if want != "auto":
        return want
    return avail[0] if avail else "none"


async def transcribe(wav: bytes, cfg: dict, language: str | None = None) -> str:
    engine = pick(cfg)
    if engine == "mlx-whisper":
        return await _whisper(wav, cfg.get("whisper_model") or "mlx-community/whisper-base-mlx", language or cfg.get("language") or None)
    if engine == "gemini":
        return await _gemini(wav, cfg.get("gemini_model") or "gemini-flash-latest")
    raise SttError("No speech engine: `pip install mlx-whisper` for free local transcription, or set GEMINI_API_KEY.")


async def _whisper(wav: bytes, model: str, language: str | None) -> str:
    import mlx_whisper
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(wav); path = f.name
    try:
        def run():
            return mlx_whisper.transcribe(path, path_or_hf_repo=model, language=language, fp16=False)
        res = await asyncio.get_running_loop().run_in_executor(None, run)
        return (res.get("text") or "").strip()
    finally:
        Path(path).unlink(missing_ok=True)


async def _gemini(wav: bytes, model: str) -> str:
    from google import genai
    from google.genai import types
    client = genai.Client(api_key=secrets.get("gemini"))
    try:
        resp = await client.aio.models.generate_content(
            model=model,
            contents=[types.Content(role="user", parts=[
                types.Part(text="Transcribe this audio verbatim. Return only the spoken words, no commentary. If there is no speech, return an empty string."),
                types.Part(inline_data=types.Blob(mime_type="audio/wav", data=wav))])],
            config=types.GenerateContentConfig(max_output_tokens=1024, temperature=0))
        return (resp.text or "").strip()
    except Exception as e:  # noqa: BLE001
        raise SttError(f"Gemini transcription failed: {str(e)[:140]}") from e
