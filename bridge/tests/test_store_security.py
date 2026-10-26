import os
from pathlib import Path

import pytest

from musecorder import security
from musecorder.chat_store import ChatStore


def test_chat_store_roundtrip(tmp_path: Path):
    st = ChatStore(tmp_path)
    c = st.new("muse/muse-spark-1.3")
    c.messages += [{"role": "user", "content": "hi"}, {"role": "assistant", "content": "hello"}]
    c.title = "hi"
    st.save(c)
    got = st.get(c.id)
    assert got and got.last_assistant() == "hello" and "> hi" in got.transcript()
    assert st.list()[0]["id"] == c.id
    assert st.delete(c.id) and st.get(c.id) is None
    with pytest.raises(ValueError):
        st.get("../etc/passwd")


def test_registry_and_tokens(tmp_path: Path):
    reg = security.Registry(tmp_path / "devices.json")
    tok = security.new_token()
    reg.add("DEV1", "Musecorder", tok, via="wifi")
    assert reg.authenticate(tok).id == "DEV1"
    assert reg.authenticate("nope") is None
    raw = (tmp_path / "devices.json").read_text()
    assert tok not in raw            # only the hash is stored
    assert reg.revoke("DEV1") and reg.authenticate(tok) is None


def test_rate_limiter():
    rl = security.RateLimiter(rate=1000, burst=2)
    assert rl.allow("a") and rl.allow("a") and not rl.allow("a")


def test_cert_fingerprint(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(security, "CERT_FILE", tmp_path / "c.crt")
    monkeypatch.setattr(security, "KEY_FILE", tmp_path / "c.key")
    monkeypatch.setattr(security.config, "TLS_DIR", tmp_path)
    fp = security.cert_fingerprint()
    assert len(fp) == 95 and fp.count(":") == 31
    assert oct(os.stat(tmp_path / "c.key").st_mode)[-3:] == "600"
