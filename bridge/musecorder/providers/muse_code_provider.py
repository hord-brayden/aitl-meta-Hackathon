"""Muse Code sessions through the `muse-code-sdk` (MSP over a spawned `muse serve`).

Each device chat maps to one Muse Code session: the first turn starts one, later turns resume it by the stored
session id (chat.ext["muse_session"]). Text streams to the device as `item/delta` arrives. Tool approvals
(`approval/requested`) are forwarded to the device as permission cards through `approval_cb`; the device's
Allow / Deny becomes `approval/decide` (allow once / deny).

If the SDK is not importable the provider falls back to `muse exec --json --session-id <uuid>` and parses its
JSONL (`run.output.delta` / `run.terminal.*` records). Headless exec cannot answer approvals.
"""
from __future__ import annotations
import asyncio
import json
import logging
import os
import shutil
import tempfile
import uuid
from pathlib import Path
from typing import Any, AsyncIterator, Awaitable, Callable

from .base import Provider, ModelInfo, ProviderError
from .. import __version__, config

log = logging.getLogger("musecorder.musecode")
PREFIX = "muse-code/"
ApprovalCb = Callable[[str, str, str], Awaitable["str | None"]]   # (tool, detail, session_id) -> "allow" | "deny" | None


def find_muse_bin(configured: str = "") -> str | None:
    if configured:
        return configured if os.path.exists(configured) else shutil.which(configured)
    found = shutil.which("muse")
    if found:
        return found
    home = Path.home() / ".local" / "bin" / "muse"
    return str(home) if home.exists() else None


def sdk_available() -> bool:
    try:
        import muse_code  # noqa: F401
        return True
    except Exception:  # noqa: BLE001  (ImportError, or a pydantic pin clash)
        return False


def pick_choice(choices: list[dict], decision: str | None) -> dict | None:
    """Map the device's answer onto one of the server-offered approval choices (never invent a choiceId)."""
    def match(dec: str, scope: str | None = None):
        for c in choices:
            if c.get("decision") == dec and (scope is None or c.get("scope") == scope):
                return c
        return None
    if decision == "allow":
        return match("approved", "once") or match("approved") or match("approvedForSession")
    return match("denied") or match("deniedPolicyAmendment") or match("abort") or next(
        (c for c in choices if str(c.get("decision", "")).startswith("denied")), None)


def parse_exec_line(line: str) -> tuple[str, str] | None:
    """`muse exec --json` JSONL -> ("delta", text) | ("done", final_text) | ("error", reason) | None."""
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        rec = json.loads(line)
    except json.JSONDecodeError:
        return None
    pt = str(rec.get("payload_type") or "")
    payload = rec.get("payload") or {}
    if pt == "run.output.delta":
        return ("delta", str(payload.get("text") or ""))
    if pt == "run.terminal.completed":
        return ("done", str(payload.get("text") or ""))
    if pt.startswith("run.terminal."):
        return ("error", str(payload.get("reason") or payload.get("text") or pt.split(".")[-1]))
    return None


class MuseCodeProvider(Provider):
    name = "muse-code"
    label = "Muse Code"

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.session_id: str | None = None      # set by the session layer per chat before each stream()
        self.last_session_id: str | None = None
        self.approval_cb: ApprovalCb | None = None
        self._client = None
        self._sessions: dict[str, Any] = {}
        self._lock = asyncio.Lock()

    # ---------- catalog ----------
    def muse_bin(self) -> str | None:
        return find_muse_bin(self.cfg.get("muse_bin") or "")

    def available(self) -> bool:
        return self.muse_bin() is not None

    def owns(self, model_id: str) -> bool:
        return model_id.startswith(PREFIX)

    async def models(self) -> list[ModelInfo]:
        ok = self.available()
        ids = list(self.cfg.get("models") or ["default"])
        return [ModelInfo(PREFIX + i, "Muse Code" if i == "default" else f"Muse Code ({i})", self.label, ok) for i in ids]

    def workspace(self) -> str:
        ws = self.cfg.get("workspace") or str(config.CONFIG_DIR / "muse-workspace")
        Path(ws).mkdir(parents=True, exist_ok=True)
        return ws

    # ---------- SDK path ----------
    async def _get_client(self):
        if self._client is not None:
            return self._client
        from muse_code import MuseClient, MuseClientSpawnOptions
        args = ["serve"]
        if self.cfg.get("provider"):
            args += ["--provider", str(self.cfg["provider"])]
        client = await MuseClient.spawn(MuseClientSpawnOptions(
            muse_bin=self.muse_bin() or "muse", args=args, cwd=self.workspace(),
            client_info={"name": "musecorder", "version": __version__},
            on_stderr=lambda line: log.debug("muse serve: %s", str(line).rstrip()[:200])))
        self._client = client
        log.info("muse serve started (%s)", (client.initialize_result.get("serverInfo") or {}).get("version", "?"))
        return client

    async def _get_session(self, session_id: str | None, model: str):
        from muse_code import StartSessionOptions, ResumeSessionOptions
        client = await self._get_client()
        if session_id and session_id in self._sessions:
            return self._sessions[session_id]
        profile = self.cfg.get("permission_profile") or None
        if session_id:
            session = await client.resume_session(ResumeSessionOptions(session_id=session_id, permission_profile=profile, on_approval=self._on_approval))
        else:
            session = await client.start_session(StartSessionOptions(workspace_root=self.workspace(), permission_profile=profile,
                                                                     model_id=None if model == "default" else model))
            session.on_approval(self._on_approval)
        session.on_approval_error(lambda failure: log.warning("approval failed: %s", getattr(failure, "kind", failure)))
        self._sessions[session.session_id] = session
        return session

    async def _on_approval(self, params: dict):
        from muse_code import ApprovalDecisionInput
        tool = str(params.get("toolName") or "tool")
        subject = params.get("subject") or {}
        detail = subject.get("command") or subject.get("path") or subject.get("host") or subject.get("target") or str(params.get("rawArgs") or "")[:200]
        choices = list(params.get("availableChoices") or [])
        decision = None
        if self.approval_cb:
            try:
                decision = await self.approval_cb(tool, str(detail), str(params.get("sessionId") or ""))
            except Exception as e:  # noqa: BLE001
                log.warning("approval relay failed: %s", e)
        choice = pick_choice(choices, decision)
        if not choice:
            raise ProviderError("no usable approval choice offered")
        feedback = None
        if decision != "allow" and choice.get("acceptsFeedback"):
            feedback = "Denied from the Musecorder device" if decision == "deny" else "No answer from the Musecorder device in time"
        log.info("approval %s (%s) -> %s", tool, decision or "timeout", choice.get("label"))
        return ApprovalDecisionInput(choice_id=str(choice["choiceId"]), feedback=feedback)

    async def _stream_sdk(self, model: str, prompt: str, session_id: str | None) -> AsyncIterator[str]:
        from muse_code import SendUserTurnOptions
        async with self._lock:
            session = await self._get_session(session_id, model)
        self.last_session_id = session.session_id
        turn = await session.send_user_turn(SendUserTurnOptions(input=[{"type": "text", "text": prompt}], composer_input=prompt))
        final: list[str] = []

        async def collect_items():
            async for it in turn.items():
                if it.get("kind") == "agentMessage" and it.get("status") == "completed" and it.get("text"):
                    final.append(str(it["text"]))
        collector = asyncio.create_task(collect_items())
        streamed = False
        try:
            async for d in turn.deltas():
                if d.get("field") in (None, "text") and d.get("delta"):     # agentMessage text; reasoning uses summary.N
                    streamed = True
                    yield str(d["delta"])
            outcome = await turn.completed
            await collector
            params = getattr(outcome, "params", {}) or {}
            if getattr(outcome, "kind", "") == "completed" and params.get("terminal") not in (None, "completed"):
                raise ProviderError(f"Muse Code: turn {params.get('terminal')}: {str(params.get('reason') or '')[:160]}")
            if not streamed and final:
                yield final[-1]
        finally:
            if not collector.done():
                collector.cancel()

    # ---------- exec fallback ----------
    async def _stream_exec(self, model: str, prompt: str, session_id: str | None) -> AsyncIterator[str]:
        exe = self.muse_bin()
        sid = session_id or str(uuid.uuid4())
        self.last_session_id = sid
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            f.write(prompt); pfile = f.name
        cmd = [exe, "exec", "--json", "--session-id", sid, "--prompt-file", pfile]
        if self.cfg.get("provider"):
            cmd += ["--provider", str(self.cfg["provider"])]
        if model != "default":
            cmd += ["--model", model]
        if self.cfg.get("reasoning_effort"):
            cmd += ["--reasoning-effort", str(self.cfg["reasoning_effort"])]
        proc = await asyncio.create_subprocess_exec(*cmd, cwd=self.workspace(), stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        streamed, done_text, err = False, "", ""
        try:
            assert proc.stdout
            async for raw in proc.stdout:
                ev = parse_exec_line(raw.decode("utf-8", "replace"))
                if not ev:
                    continue
                kind, text = ev
                if kind == "delta" and text:
                    streamed = True; yield text
                elif kind == "done":
                    done_text = text
                elif kind == "error":
                    err = text
            await proc.wait()
            if err:
                raise ProviderError(f"Muse Code: {err[:160]}")
            if proc.returncode not in (0, None) and not (streamed or done_text):
                stderr = (await proc.stderr.read()).decode("utf-8", "replace") if proc.stderr else ""
                tail = [ln for ln in stderr.splitlines() if ln.strip() and not ln.startswith("muse: workspace")]
                raise ProviderError(f"Muse Code exited {proc.returncode}: {(tail[-1] if tail else '')[:160]}")
            if not streamed and done_text:
                yield done_text
        finally:
            Path(pfile).unlink(missing_ok=True)
            if proc.returncode is None:
                proc.kill()

    # ---------- Provider API ----------
    async def stream(self, model_id: str, system: str, messages: list[dict], max_tokens: int) -> AsyncIterator[str]:
        if not self.available():
            raise ProviderError("Muse Code not found. Install the `muse` CLI (pip install muse-code) and run `muse login`.")
        model = model_id[len(PREFIX):]
        session_id = self.session_id
        latest = messages[-1]["content"] if messages else ""
        prompt = latest
        if not session_id and len(messages) > 1:
            # Imported or pre-existing history: fold earlier turns into the first prompt of the new session.
            hist = "\n\n".join(("User: " if m["role"] == "user" else "Assistant: ") + m["content"] for m in messages[:-1])
            prompt = f"Conversation so far (for context):\n\n{hist[-12000:]}\n\n---\nUser: {latest}"
        if system and not session_id:
            prompt = f"{system}\n\n{prompt}"            # the host owns its system prompt; ours rides along once per session
        gen = self._stream_sdk if sdk_available() else self._stream_exec
        try:
            async for piece in gen(model, prompt, session_id):
                yield piece
        except ProviderError:
            raise
        except asyncio.CancelledError:
            raise
        except Exception as e:  # noqa: BLE001
            raise ProviderError(f"Muse Code: {type(e).__name__}: {str(e)[:160]}") from e

    # ---------- session browsing (for cc.list / cc.open) ----------
    async def _request(self, method: str, params: dict) -> dict:
        client = await self._get_client()
        conn = getattr(client, "_connection", None) or getattr(client, "connection", None)
        if conn is None:
            raise ProviderError("SDK client exposes no connection")
        return dict(await conn.request(method, params))

    async def list_sessions(self, limit: int = 15) -> list[dict]:
        if not (self.available() and sdk_available()):
            return []
        res = await self._request("session/list", {"limit": min(max(int(limit), 1), 200)})
        return [dict(s) for s in res.get("sessions", []) if s.get("kind", "root") == "root"]

    async def read_session(self, session_id: str) -> dict:
        if not (self.available() and sdk_available()):
            return {}
        try:
            return await self._request("session/read", {"sessionId": session_id})
        except Exception as e:  # noqa: BLE001
            log.debug("session/read failed: %s", e)
            return {}

    async def close(self) -> None:
        if self._client is not None:
            try:
                await self._client.close()
            except Exception:  # noqa: BLE001
                pass
            self._client = None
            self._sessions.clear()
