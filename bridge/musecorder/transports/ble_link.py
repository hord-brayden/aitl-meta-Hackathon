"""BLE transport (Nordic UART Service) using bleak: scans for the AITL device, subscribes to TX, writes RX.
Same NDJSON frames as serial. Enable with `musecorder serve --ble` (and Setup > Bluetooth link on the device)."""
from __future__ import annotations
import asyncio
import logging
from typing import Any

from .. import protocol
from ..session import DeviceSession, Shared

log = logging.getLogger("musecorder.ble")
NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


class BleTransport:
    def __init__(self, shared: Shared, name_prefix: str = "AITL"):
        self.shared, self.prefix = shared, name_prefix
        self.client = None
        self.session: DeviceSession | None = None
        self._buf = b""
        self._lock = asyncio.Lock()

    async def send(self, msg: dict[str, Any]) -> None:
        if not self.client or not self.client.is_connected:
            return
        data = protocol.encode(msg)
        mtu = max(20, (getattr(self.client, "mtu_size", 23) or 23) - 3)
        async with self._lock:
            for i in range(0, len(data), mtu):
                await self.client.write_gatt_char(NUS_RX, data[i:i + mtu], response=False)
                await asyncio.sleep(0.004)

    def _on_notify(self, _sender, data: bytearray) -> None:
        self._buf += bytes(data)
        while b"\n" in self._buf:
            line, self._buf = self._buf.split(b"\n", 1)
            if line.startswith(b"#"):
                log.info("device %s", line[2:].decode("utf-8", "replace")); continue
            msg = protocol.decode(line)
            if msg and self.session:
                asyncio.create_task(self._handle(msg))

    async def _handle(self, msg: dict[str, Any]) -> None:
        try:
            await self.session.handle(msg)
        except Exception:  # noqa: BLE001
            log.exception("ble handler error")

    async def run(self) -> None:
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError:
            log.error("bleak not installed: pip install bleak"); return
        while True:
            try:
                dev = await BleakScanner.find_device_by_filter(
                    lambda d, ad: (d.name or ad.local_name or "").startswith(self.prefix), timeout=8.0)
                if not dev:
                    self._misses = getattr(self, "_misses", 0) + 1
                    if self._misses in (1, 5, 20):
                        log.info("ble: no '%s*' device seen yet (device: Setup > Bluetooth keyboard + Bluetooth link on)", self.prefix)
                    await asyncio.sleep(3); continue
                log.info("ble: connecting to %s (%s)", dev.name, dev.address)
                disconnected = asyncio.Event()
                async with BleakClient(dev, disconnected_callback=lambda c: disconnected.set()) as client:
                    self.client = client
                    self.session = DeviceSession(self.shared, self.send, transport="ble", peer=dev.address)
                    self.session.authenticated = True   # a BLE pairing is a physical act; the host chose to connect
                    if NUS_SVC not in [s.uuid.lower() for s in client.services]:
                        log.warning("ble: %s has no Nordic UART service (enable 'Bluetooth link' on the device)", dev.name)
                        await asyncio.sleep(10); continue
                    await client.start_notify(NUS_TX, self._on_notify)
                    log.info("ble: linked, mtu %s", getattr(client, "mtu_size", "?"))
                    await disconnected.wait()
            except Exception as e:  # noqa: BLE001
                log.warning("ble: %s", str(e)[:160])
            finally:
                if self.session:
                    await self.session.close()
                self.session = None; self.client = None; self._buf = b""
            await asyncio.sleep(2)


async def scan_once(seconds: float = 6.0) -> list[str]:
    """Debug helper for `musecorder ble-scan`: names, addresses and advertised services."""
    from bleak import BleakScanner
    found = await BleakScanner.discover(timeout=seconds, return_adv=True)
    out = []
    for dev, adv in found.values():
        name = dev.name or adv.local_name or "?"
        out.append(f"{name:24s} {dev.address}  rssi {adv.rssi}  {[u[:8] for u in (adv.service_uuids or [])]}")
    return sorted(out)
