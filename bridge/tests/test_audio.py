import math, struct
from musecorder import audio, stt


def test_adpcm_roundtrip_sine():
    pcm = b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * i / 16000))) for i in range(1600))
    enc = audio.AdpcmEncoder().encode(pcm)
    assert len(enc) == len(pcm) // 4
    dec = audio.AdpcmDecoder().decode(enc)
    assert len(dec) == len(pcm)
    a = struct.unpack("<1600h", pcm); b = struct.unpack("<1600h", dec)
    err = (sum((x - y) ** 2 for x, y in zip(a[200:], b[200:])) / 1400) ** 0.5
    assert err < 900            # ADPCM noise well below the 12000-amplitude signal
    assert audio.rms(dec) > 7000
    wav = audio.pcm_to_wav(dec)
    assert wav[:4] == b"RIFF" and len(wav) == 44 + len(dec)


def test_engine_pick(monkeypatch):
    monkeypatch.setattr(stt, "available_engines", lambda: ["gemini"])
    assert stt.pick({"engine": "auto"}) == "gemini"
    assert stt.pick({"engine": "mlx-whisper"}) == "mlx-whisper"
    monkeypatch.setattr(stt, "available_engines", lambda: [])
    assert stt.pick({}) == "none"
