"""Assemble the device's `screen.row` stream (big-endian RGB565 rows, base64) into a PNG. Pure Python: no Pillow."""
from __future__ import annotations
import base64
import struct
import zlib


def rows_to_png(rows: dict[int, bytes], w: int, h: int) -> bytes:
    raw = bytearray()
    blank = b"\x00" * (w * 2)
    for y in range(h):
        raw.append(0)                                   # PNG filter: none
        r = rows.get(y, blank)
        if len(r) < w * 2:
            r = r + b"\x00" * (w * 2 - len(r))
        for i in range(w):
            v = (r[2 * i] << 8) | r[2 * i + 1]
            raw += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def decode_row(frame: dict) -> tuple[int, bytes]:
    return int(frame.get("y", 0)), base64.b64decode(frame.get("d", ""))
