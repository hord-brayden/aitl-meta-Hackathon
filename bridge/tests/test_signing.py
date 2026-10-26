from musecorder.signing import Signer, key_from_token, key_from_token_hash
import hashlib

def test_roundtrip_and_replay():
    tok = "ab" * 32
    k = key_from_token(tok)
    assert k == key_from_token_hash(hashlib.sha256(tok.encode()).hexdigest())
    a, b = Signer(k, sid="s1"), Signer(k, sid="s1")
    frame = a.wrap({"t": "ping"})
    assert b.unwrap(frame) == {"t": "ping"}
    assert b.unwrap(frame) is None                      # replay rejected
    bad = Signer(key_from_token("other"), sid="s1").wrap({"t": "hid.type", "text": "x"})
    assert b.unwrap(bad) is None                        # wrong key rejected
    assert Signer(k, sid="s2").unwrap(a.wrap({"t": "x"})) is None   # wrong session rejected
