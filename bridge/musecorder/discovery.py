"""Optional mDNS advertisement so a device (or `aitl` on another Mac) can find the bridge: _aitl._tcp.local."""
from __future__ import annotations
import logging
import socket

log = logging.getLogger("musecorder.discovery")


class Advertiser:
    def __init__(self, name: str, port: int, fingerprint: str):
        self.name, self.port, self.fp = name, port, fingerprint
        self.zc = None
        self.info = None

    def start(self) -> bool:
        """Registers in a background thread: zeroconf's sync API must not run inside the asyncio loop, and
        discovery is a convenience, so any failure is logged and ignored."""
        import threading
        threading.Thread(target=self._register, name="aitl-mdns", daemon=True).start()
        return True

    def _register(self) -> None:
        try:
            from zeroconf import Zeroconf, ServiceInfo
        except ImportError:
            log.info("zeroconf not installed; skipping mDNS (pip install 'musecorder[discovery]')")
            return
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(("10.255.255.255", 1)); ip = s.getsockname()[0]; s.close()
            name = self.name.replace(".", "-")
            # server = the stable host name devices connect to (also what the TLS certificate is issued for)
            self.info = ServiceInfo("_aitl._tcp.local.", f"{name}._aitl._tcp.local.", addresses=[socket.inet_aton(ip)],
                                    port=self.port, properties={"fp": self.fp[:23], "v": "1"}, server="aitl-bridge.local.")
            self.zc = Zeroconf()
            self.zc.register_service(self.info)
            log.info("mDNS: aitl-bridge.local -> %s:%d (service %s._aitl._tcp)", ip, self.port, name)
        except Exception as e:  # noqa: BLE001
            log.info("mDNS advertisement skipped: %s", str(e)[:120])

    def stop(self) -> None:
        try:
            if self.zc and self.info:
                self.zc.unregister_service(self.info)
                self.zc.close()
        except Exception:  # noqa: BLE001
            pass
