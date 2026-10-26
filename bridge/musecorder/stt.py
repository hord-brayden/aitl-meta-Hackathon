"""Speech-to-text engines for device-microphone dictation. Picked by config `stt.engine`:
  auto         -> mlx-whisper if installed (local, free, Apple Silicon), else muse-cli, else muse-api
  mlx-whisper  -> pip install mlx-whisper   (model downloads on first use; see stt.whisper_model)
  muse-cli     -> `muse voice transcribe <wav>` (the installed Muse Code CLI; needs `muse login`)
  muse-api     -> Meta Model API, model muse-voice-transcribe-1.0, POST /v1/asr/transcribe (needs META_API_KEY)
"""
from __future__ import annotations
import asyncio
import logging
import shutil
import tempfile
from pathlib import Path

from . import secrets, config

log = logging.getLogger("musecorder.stt")


class SttError(RuntimeError):
    pass


def _muse_bin(cfg: dict | None = None) -> str | None:
    from .providers.muse_code_provider import find_muse_bin
    return find_muse_bin((cfg or {}).get("muse_bin") or "")


def available_engines(cfg: dict | None = None) -> list[str]:
    out = []
    try:
        import mlx_whisper  # noqa: F401
        out.append("mlx-whisper")
    except ImportError:
        pass
    if _muse_bin(cfg):
        out.append("muse-cli")
    if secrets.get("muse"):
        out.append("muse-api")
    return out


def pick(cfg: dict) -> str:
    want = (cfg.get("engine") or "auto").lower()
    avail = available_engines(cfg)
    if want != "auto":
        return want
    return avail[0] if avail else "none"


async def transcribe(wav: bytes, cfg: dict, language: str | None = None) -> str:
    engine = pick(cfg)
    if engine == "mlx-whisper":
        return await _whisper(wav, cfg.get("whisper_model") or "mlx-community/whisper-base-mlx", language or cfg.get("language") or None)
    if engine == "muse-cli":
        return await _muse_cli(wav, cfg)
    if engine == "muse-api":
        return await _muse_api(wav)
    raise SttError("No speech engine: `pip install mlx-whisper` for free local transcription, run `muse login`, or set META_API_KEY.")


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


async def _muse_cli(wav: bytes, cfg: dict) -> str:
    """`muse voice transcribe <file>` prints the spoken words."""
    exe = _muse_bin(cfg)
    if not exe:
        raise SttError("muse CLI not found")
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        f.write(wav); path = f.name
    try:
        proc = await asyncio.create_subprocess_exec(exe, "voice", "transcribe", path,
                                                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        try:
            out, err = await asyncio.wait_for(proc.communicate(), 60)
        except asyncio.TimeoutError:
            proc.kill(); raise SttError("muse voice transcribe timed out")
        if proc.returncode != 0:
            tail = err.decode("utf-8", "replace").strip().splitlines()
            raise SttError(f"muse voice transcribe failed: {(tail[-1] if tail else '')[:140]} (run `muse login`?)")
        return out.decode("utf-8", "replace").strip()
    finally:
        Path(path).unlink(missing_ok=True)


async def _muse_api(wav: bytes) -> str:
    """Meta Model API file transcription. dev.meta.ai documents the model (muse-voice-transcribe-1.0) and the endpoint
    (POST /v1/asr/transcribe) but the request body shape was not published at the time of writing; this sends a
    multipart upload (`file` + `model`) and reads `text` / `transcript` from the JSON reply.
    TODO: confirm the body against https://dev.meta.ai/docs/api-reference/voice/transcribe."""
    import httpx
    key = secrets.get("muse")
    if not key:
        raise SttError("META_API_KEY missing (musecorder keys set muse)")
    url = f"{config.MUSE_API_BASE}/asr/transcribe"
    try:
        async with httpx.AsyncClient(timeout=60) as c:
            r = await c.post(url, headers={"Authorization": f"Bearer {key}"},
                             files={"file": ("audio.wav", wav, "audio/wav")}, data={"model": config.MUSE_VOICE_MODEL})
            if r.status_code != 200:
                raise SttError(f"Muse Voice Transcribe HTTP {r.status_code}: {r.text[:140]}")
            body = r.json()
    except httpx.HTTPError as e:
        raise SttError(f"Muse Voice Transcribe unreachable: {type(e).__name__}") from e
    text = body.get("text") or body.get("transcript") or ""
    if not text and isinstance(body.get("turns"), list):
        text = " ".join(str(t.get("text") or "") for t in body["turns"])
    if not text and isinstance(body.get("segments"), list):
        text = " ".join(str(t.get("text") or "") for t in body["segments"])
    return str(text).strip()
