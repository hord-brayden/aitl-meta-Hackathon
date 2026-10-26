"""Protocol-level tests with a fake provider: no network, no Mac automation."""
import asyncio
from pathlib import Path

import pytest

from musecorder import config
from musecorder.providers.base import Provider, ModelInfo
from musecorder.session import Shared, DeviceSession


class FakeProvider(Provider):
    name = "fake"; label = "Fake"
    def available(self): return True
    def owns(self, model_id): return model_id.startswith("fake/")
    async def models(self): return [ModelInfo("fake/echo", "Echo", "Fake", True)]
    async def stream(self, model_id, system, messages, max_tokens):
        for w in ("echo: " + messages[-1]["content"]).split(" "):
            yield w + " "


@pytest.fixture
def shared(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(config, "CONFIG_DIR", tmp_path); monkeypatch.setattr(config, "CHATS_DIR", tmp_path / "chats")
    monkeypatch.setattr(config, "TLS_DIR", tmp_path / "tls"); monkeypatch.setattr(config, "DEVICES_FILE", tmp_path / "d.json")
    monkeypatch.setattr(config, "CONFIG_FILE", tmp_path / "config.toml")
    cfg = config.load()
    cfg["bridge"]["default_model"] = "fake/echo"
    sh = Shared(cfg)
    sh.router.providers = [FakeProvider()]
    sh.chats.root = tmp_path / "chats"; sh.chats.root.mkdir(exist_ok=True)
    return sh


async def test_hello_and_chat(shared):
    out = []
    async def send(m): out.append(m)
    s = DeviceSession(shared, send, transport="serial", peer="test")
    await s.handle({"t": "hello", "id": "DEV", "fw": "0.1.0"})
    ack = next(m for m in out if m["t"] == "hello.ack")
    assert ack["model"] == "fake/echo" and ack["models"][0]["ok"]
    await s.handle({"t": "chat.send", "text": "hello there", "chat": "", "model": "fake/echo"})
    await s.gen_task
    deltas = "".join(m["d"] for m in out if m["t"] == "chat.delta")
    assert deltas.startswith("echo: hello there")
    done = next(m for m in out if m["t"] == "chat.done")
    assert done["title"] == "hello there"
    assert shared.chats.get(done["chat"]).last_assistant().startswith("echo:")


async def test_unauthenticated_wifi_is_blocked(shared):
    out = []
    async def send(m): out.append(m)
    s = DeviceSession(shared, send, transport="wifi", peer="1.2.3.4:5")
    await s.handle({"t": "chat.send", "text": "x"})
    assert out and out[-1]["t"] == "ui.toast" and "paired" in out[-1]["msg"].lower()
