from musecorder import protocol


def test_roundtrip():
    msg = {"t": "chat.delta", "d": "héllo ✓"}
    assert protocol.decode(protocol.encode(msg)) == msg


def test_ignores_logs_and_garbage():
    assert protocol.decode(b"# boot ok") is None
    assert protocol.decode(b"{not json") is None
    assert protocol.decode(b'{"x":1}') is None


def test_chunks_and_fits():
    text = "a" * 1000
    parts = list(protocol.chunks(text, 160))
    assert "".join(parts) == text and all(len(p) <= 160 for p in parts)
    assert protocol.fits({"t": "chat.delta", "d": "x" * 100})
    assert not protocol.fits({"t": "chat.view", "text": "x" * 10000})
