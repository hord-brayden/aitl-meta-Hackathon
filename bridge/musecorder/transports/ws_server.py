"""TLS WebSocket server for Wi-Fi devices. Bearer token per device; `pairing` token only during a pairing window."""
from __future__ import annotations
import asyncio
import logging
import ssl
from typing import Any

import websockets
from websockets.asyncio.server import serve, ServerConnection

from .. import protocol, security
from ..session import DeviceSession, Shared
from ..signing import Signer, key_from_token_hash

log = logging.getLogger("musecorder.ws")


class WsServer:
    def __init__(self, shared: Shared, bind: str = "0.0.0.0", port: int = 7777, mode: str = "signed"):
        self.shared, self.bind, self.port, self.mode = shared, bind, port, mode

    async def _handler(self, ws: ServerConnection) -> None:
        peer = f"{ws.remote_address[0]}:{ws.remote_address[1]}" if ws.remote_address else "?"
        auth = ws.request.headers.get("Authorization", "") if ws.request else ""
        token = auth[7:].strip() if auth.startswith("Bearer ") else ""
        if ws.request and ws.request.path != "/ws":
            await ws.close(code=1008, reason="bad path"); return

        signer: Signer | None = None
        if self.mode == "signed":
            # Signed mode: the device never sends its token. The Authorization header carries only the device id;
            # frames prove possession of the token through the HMAC.
            dev_id = ws.request.headers.get("X-AITL-Device", "") if ws.request else ""
            device = next((d for d in self.shared.registry.devices.values() if d.id == dev_id), None) if dev_id else None
            if token == "pairing":
                pass
            elif not device:
                log.warning("rejected signed connection from %s (unknown device %r)", peer, dev_id)
                await ws.close(code=1008, reason="unknown device"); return
            else:
                signer = Signer(key_from_token_hash(device.token_hash))
        else:
            device = self.shared.registry.authenticate(token) if token and token != "pairing" else None

        async def send(msg: dict[str, Any]) -> None:
            if signer:
                await ws.send(signer.wrap(msg))
            else:
                await ws.send(protocol.encode(msg).decode("utf-8"))

        session = DeviceSession(self.shared, send, transport="wifi", peer=peer)
        if device and (signer or self.mode != "signed"):
            session.authenticated = True
            session.device_id = device.id
            log.info("wifi device %s %s from %s", device.id, "signed-link" if signer else "authenticated", peer)
        elif token == "pairing":
            log.info("pairing attempt from %s", peer)
        else:
            log.warning("rejected connection from %s (bad token)", peer)
            await ws.close(code=1008, reason="unauthorized"); return
        try:
            async for raw in ws:
                text = raw if isinstance(raw, str) else (raw.decode("utf-8", "replace") if isinstance(raw, bytes) else str(raw))
                if signer:
                    # the device's first frame has no session nonce yet; every later frame must carry ours
                    msg = signer.unwrap(text, allow_no_sid=(signer.rx_last < 0))
                    if not msg:
                        log.warning("dropped unverifiable frame from %s", peer); continue
                else:
                    msg = protocol.decode(text)
                if not msg:
                    continue
                if msg.get("t") == "hello" and not session.authenticated:
                    session.device_id = str(msg.get("id", ""))[:32]; session.fw = str(msg.get("fw", ""))[:16]
                    await session.offer_pairing(); continue
                await session.handle(msg)
        except websockets.ConnectionClosed:
            pass
        finally:
            await session.close()
            log.info("wifi %s disconnected", peer)

    async def run(self) -> None:
        ctx = None
        if self.mode == "tls":
            cert, key = security.ensure_cert()
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.minimum_version = ssl.TLSVersion.TLSv1_2
            ctx.load_cert_chain(str(cert), str(key))
        async with serve(self._handler, self.bind, self.port, ssl=ctx, max_size=64 * 1024, ping_interval=20, ping_timeout=20):
            if ctx:
                log.info("wifi: wss://%s:%d/ws  fingerprint %s", self.bind, self.port, security.cert_fingerprint())
            else:
                log.info("wifi: ws://%s:%d/ws  (signed frames: HMAC-SHA256 per frame, per-session nonce)", self.bind, self.port)
            await asyncio.Future()
