"""USB serial transport: auto-detects the CYD's CH340 port, reconnects on unplug, feeds NDJSON to a session."""
from __future__ import annotations
import asyncio
import glob
import logging
from typing import Any

import serial  # pyserial

from .. import protocol
from ..session import DeviceSession, Shared

log = logging.getLogger("musecorder.serial")
PATTERNS = ["/dev/cu.usbserial-*", "/dev/cu.wchusbserial*", "/dev/cu.SLAB_USBtoUART*", "/dev/ttyUSB*", "/dev/ttyACM*"]


def find_port(preferred: str = "auto") -> str | None:
    if preferred and preferred not in ("auto", "off"):
        return preferred
    for pat in PATTERNS:
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[0]
    return None


class SerialTransport:
    def __init__(self, shared: Shared, port: str = "auto", baud: int = 115200):
        self.shared, self.port_pref, self.baud = shared, port, baud
        self.ser: serial.Serial | None = None
        self.session: DeviceSession | None = None
        self._lock = asyncio.Lock()

    async def send(self, msg: dict[str, Any]) -> None:
        if not self.ser:
            return
        data = protocol.encode(msg)
        async with self._lock:
            loop = asyncio.get_running_loop()
            await loop.run_in_executor(None, self.ser.write, data)
            if len(data) > 1024:                       # give the device time to drain big frames while it redraws
                await asyncio.sleep(min(0.4, len(data) / 12000))

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        while True:
            port = find_port(self.port_pref)
            if not port:
                await asyncio.sleep(2); continue
            try:
                self.ser = serial.Serial(None, self.baud, timeout=0.2, dsrdtr=False, rtscts=False)
                self.ser.port = port
                self.ser.dtr = False; self.ser.rts = False     # set BEFORE open so the CH340 never pulses EN/IO0
                self.ser.open()
            except (serial.SerialException, OSError) as e:
                log.warning("serial open failed (%s): %s", port, e)
                await asyncio.sleep(3); continue
            log.info("serial: %s @ %d", port, self.baud)
            self.session = DeviceSession(self.shared, self.send, transport="serial", peer=port)
            buf = b""
            try:
                while True:
                    chunk = await loop.run_in_executor(None, self.ser.read, 4096)
                    if chunk:
                        buf += chunk
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            line = line.strip(b"\r")
                            if line.startswith(b"#"):
                                log.info("device %s", line[2:].decode("utf-8", "replace"))
                                continue
                            if not line.startswith(b"{"):
                                txt = line.decode("utf-8", "replace")
                                if any(k in txt for k in ("Guru Meditation", "Backtrace:", "abort()", "assert failed", "rst:0x", "ELF file", "EXCVADDR", "PC      :", "Core  1", "Core  0", "panic")):
                                    log.warning("device CRASH %s", txt[:400])
                                elif "][E][" in txt or "][W][" in txt:
                                    log.warning("device %s", txt[:300])
                                continue
                            msg = protocol.decode(line)
                            if msg:
                                try:
                                    await self.session.handle(msg)
                                except Exception:  # noqa: BLE001
                                    log.exception("handler error")
                        if len(buf) > 65536:
                            buf = b""
            except (serial.SerialException, OSError) as e:
                log.warning("serial lost: %s", e)
            finally:
                if self.session:
                    await self.session.close()
                self.session = None
                try:
                    self.ser.close()
                except Exception:  # noqa: BLE001
                    pass
                self.ser = None
            await asyncio.sleep(1.5)
