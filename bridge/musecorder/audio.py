"""IMA ADPCM decoding (matches firmware/src/hal/mic.cpp) and WAV packing for the STT engines."""
from __future__ import annotations
import io
import struct
import wave

INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
STEP_TABLE = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]


class AdpcmDecoder:
    def __init__(self):
        self.pred = 0
        self.index = 0

    def _sample(self, code: int) -> int:
        step = STEP_TABLE[self.index]
        delta = step >> 3
        if code & 4: delta += step
        if code & 2: delta += step >> 1
        if code & 1: delta += step >> 2
        self.pred += -delta if code & 8 else delta
        self.pred = max(-32768, min(32767, self.pred))
        self.index = max(0, min(88, self.index + INDEX_TABLE[code & 7]))
        return self.pred

    def decode(self, data: bytes) -> bytes:
        out = bytearray()
        for b in data:
            out += struct.pack("<h", self._sample(b & 0x0F))
            out += struct.pack("<h", self._sample(b >> 4))
        return bytes(out)


class AdpcmEncoder:
    """Reference encoder (used by tests to round-trip audio)."""
    def __init__(self):
        self.pred = 0
        self.index = 0

    def _code(self, sample: int) -> int:
        step = STEP_TABLE[self.index]
        diff = sample - self.pred
        code = 0
        if diff < 0: code, diff = 8, -diff
        tmp, delta = step, 0
        if diff >= tmp: code |= 4; diff -= tmp; delta += tmp
        tmp >>= 1
        if diff >= tmp: code |= 2; diff -= tmp; delta += tmp
        tmp >>= 1
        if diff >= tmp: code |= 1; delta += tmp
        delta += step >> 3
        self.pred += -delta if code & 8 else delta
        self.pred = max(-32768, min(32767, self.pred))
        self.index = max(0, min(88, self.index + INDEX_TABLE[code & 7]))
        return code

    def encode(self, pcm: bytes) -> bytes:
        samples = struct.unpack(f"<{len(pcm) // 2}h", pcm)
        out = bytearray()
        for i in range(0, len(samples) - 1, 2):
            out.append((self._code(samples[i]) & 0x0F) | (self._code(samples[i + 1]) << 4))
        return bytes(out)


def pcm_to_wav(pcm: bytes, rate: int = 16000) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm)
    return buf.getvalue()


def rms(pcm: bytes) -> float:
    n = len(pcm) // 2
    if not n:
        return 0.0
    samples = struct.unpack(f"<{n}h", pcm[:n * 2])
    return (sum(s * s for s in samples) / n) ** 0.5
