"""TLS material, device tokens and the pairing registry.

Model: the device pins the bridge's self-signed certificate (sent over USB or confirmed by fingerprint +
code during Wi-Fi pairing) and authenticates with a per-device bearer token. Only a SHA-256 of each token is
stored here; the plaintext exists on the device alone.
"""
from __future__ import annotations
import datetime as dt
import hashlib
import hmac
import ipaddress
import json
import os
import secrets
import socket
import time
from dataclasses import dataclass, asdict, field
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

from . import config

CERT_FILE = config.TLS_DIR / "bridge.crt"
KEY_FILE = config.TLS_DIR / "bridge.key"


def _local_ips() -> list[str]:
    ips: set[str] = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except socket.gaierror:
        pass
    try:  # the interface that routes to the LAN
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return sorted(i for i in ips if not i.startswith("127."))


BRIDGE_DNS = "aitl-bridge.local"   # stable name the device connects to (advertised over mDNS), independent of the Mac's IP


def ensure_cert(force: bool = False) -> tuple[Path, Path]:
    """Create a 10-year self-signed EC P-256 certificate if none exists. ESP32 mbedTLS verifies it as its own CA.
    The SAN carries the stable mDNS name plus the current IPs, so devices can pin it by name."""
    config.ensure_dirs()
    if CERT_FILE.exists() and KEY_FILE.exists() and not force:
        return CERT_FILE, KEY_FILE
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "aitl-bridge"),
                      x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Caeruli Obscuri Laboratorium")])
    san = [x509.DNSName(BRIDGE_DNS), x509.DNSName("aitl-bridge"), x509.DNSName(socket.gethostname().split(".")[0] + ".local"), x509.DNSName("localhost")]
    for ip in _local_ips():
        try:
            san.append(x509.IPAddress(ipaddress.ip_address(ip)))
        except ValueError:
            pass
    now = dt.datetime.now(dt.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - dt.timedelta(days=1))
            .not_valid_after(now + dt.timedelta(days=3650))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName(san), critical=False)
            .add_extension(x509.KeyUsage(digital_signature=True, key_encipherment=False, key_cert_sign=True, crl_sign=False,
                                         content_commitment=False, data_encipherment=False, key_agreement=True,
                                         encipher_only=False, decipher_only=False), critical=True)
            .sign(key, hashes.SHA256()))
    KEY_FILE.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                           serialization.NoEncryption()))
    os.chmod(KEY_FILE, 0o600)
    CERT_FILE.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    return CERT_FILE, KEY_FILE


def cert_pem() -> str:
    ensure_cert()
    return CERT_FILE.read_text()


def cert_fingerprint() -> str:
    ensure_cert()
    cert = x509.load_pem_x509_certificate(CERT_FILE.read_bytes())
    fp = cert.fingerprint(hashes.SHA256()).hex().upper()
    return ":".join(fp[i:i + 2] for i in range(0, len(fp), 2))


def new_token() -> str:
    return secrets.token_hex(32)


def token_hash(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def pairing_code() -> str:
    return f"{secrets.randbelow(1_000_000):06d}"


@dataclass
class Device:
    id: str
    name: str
    token_hash: str
    paired_at: float
    via: str
    last_seen: float = 0.0
    fw: str = ""


class Registry:
    def __init__(self, path: Path = config.DEVICES_FILE):
        self.path = path
        self.devices: dict[str, Device] = {}
        self.load()

    def load(self) -> None:
        if self.path.exists():
            raw = json.loads(self.path.read_text() or "{}")
            self.devices = {k: Device(**v) for k, v in raw.items()}

    def save(self) -> None:
        config.ensure_dirs()
        self.path.write_text(json.dumps({k: asdict(v) for k, v in self.devices.items()}, indent=2))
        os.chmod(self.path, 0o600)

    def add(self, device_id: str, name: str, token: str, via: str, fw: str = "") -> Device:
        d = Device(id=device_id, name=name, token_hash=token_hash(token), paired_at=time.time(), via=via, fw=fw)
        self.devices[device_id] = d
        self.save()
        return d

    def authenticate(self, token: str) -> Device | None:
        h = token_hash(token)
        for d in self.devices.values():
            if hmac.compare_digest(d.token_hash, h):
                d.last_seen = time.time()
                return d
        return None

    def revoke(self, device_id: str) -> bool:
        if device_id in self.devices:
            del self.devices[device_id]
            self.save()
            return True
        return False


class RateLimiter:
    """Token bucket per client key (pairing attempts, HID actions)."""
    def __init__(self, rate: float, burst: int):
        self.rate, self.burst = rate, burst
        self.state: dict[str, tuple[float, float]] = {}

    def allow(self, key: str) -> bool:
        now = time.monotonic()
        tokens, last = self.state.get(key, (float(self.burst), now))
        tokens = min(self.burst, tokens + (now - last) * self.rate)
        if tokens < 1:
            self.state[key] = (tokens, now)
            return False
        self.state[key] = (tokens - 1, now)
        return True
