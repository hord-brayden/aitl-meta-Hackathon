"""Validator + chunker (musecorder.applets): pure, no device."""
import json
from pathlib import Path

import pytest

from musecorder import applets, protocol

EXAMPLES = sorted((Path(__file__).resolve().parents[2] / "examples" / "applets").glob("*.json"))
SKILL_EXAMPLES = sorted((Path(__file__).resolve().parents[2] / ".agents" / "skills" / "musecorder" / "examples" / "applets").glob("*.json"))


def minimal(**over):
    doc = {"id": "tally", "name": "Tally", "state": {"count": 0}, "ui": [{"type": "label", "text": "${count}", "size": "xxl"}],
           "on": {"start": [{"set": {"count": 0}}]}}
    doc.update(over)
    return doc


@pytest.mark.parametrize("path", EXAMPLES + SKILL_EXAMPLES, ids=lambda p: p.parent.parent.parent.name + "/" + p.name)
def test_examples_validate(path):
    doc, text = applets.load(path)
    assert applets.validate(doc) == []
    assert len(text.encode()) <= applets.MAX_DOC_BYTES


def test_four_examples_present():
    assert {p.name for p in EXAMPLES} == {"door-tally.json", "pomodoro.json", "dice.json", "pin-watch.json"}


def test_minimal_ok_and_text_input():
    assert applets.validate(minimal()) == []
    assert applets.validate(json.dumps(minimal())) == []
    assert applets.validate("{not json")[0].startswith("invalid JSON")
    assert applets.validate("[]") == ["document must be a JSON object"]


def test_required_and_id_rules():
    errs = applets.validate({"ui": []})
    assert any("missing required key 'id'" in e for e in errs) and any("'name'" in e for e in errs)
    assert any("id must match" in e for e in applets.validate(minimal(id="Bad_ID")))
    assert any("id must match" in e for e in applets.validate(minimal(id="x" * 25)))
    assert any("name must be" in e for e in applets.validate(minimal(name="a" * 21)))
    assert any("icon" in e for e in applets.validate(minimal(icon="not-a-symbol")))
    assert applets.validate(minimal(icon="ok")) == [] and applets.validate(minimal(icon="42")) == []
    assert any("unknown top-level key" in e for e in applets.validate(minimal(extra=1)))
    assert any("version must be an int" in e for e in applets.validate(minimal(version="1")))


def test_widgets_and_actions():
    errs = applets.validate(minimal(ui=[{"type": "gauge"}]))
    assert any("unknown widget type 'gauge'" in e for e in errs)
    errs = applets.validate(minimal(ui=[{"type": "label", "text": "x", "size": "huge", "bogus": 1}]))
    assert any("size must be" in e for e in errs) and any("unknown field 'bogus'" in e for e in errs)
    errs = applets.validate(minimal(ui=[{"type": "button", "text": "go", "do": [{"explode": True}]}]))
    assert any("unknown action 'explode'" in e for e in errs)
    errs = applets.validate(minimal(ui=[{"type": "button", "text": "go", "do": [{"inc": "count", "dec": "count"}]}]))
    assert any("exactly one verb key" in e for e in errs)
    errs = applets.validate(minimal(ui=[{"type": "toggle", "text": "t", "bind": "nope"}]))
    assert any("not a state key" in e for e in errs)
    errs = applets.validate(minimal(on={"start": [{"led": "purple"}, {"http": {"url": "x", "method": "TRACE", "timeout": 99, "via": "cloud"}}]}))
    assert any("led must be" in e for e in errs) and any("GET/POST/PUT" in e for e in errs)
    assert any("timeout" in e for e in errs) and any("via" in e for e in errs)
    errs = applets.validate(minimal(on={"start": [{"gpio.mode": {"pin": 99, "mode": "out"}}, {"i2c.read": {"len": 2}}, {"if": "${count}", "then": [{"nope": 1}]}, {"if": 3, "then": [], "bogus": 1}]}))
    assert any("pin must be" in e for e in errs) and any("needs addr" in e for e in errs) and any("on.start[2].then[0]" in e for e in errs)
    assert any("on.start[3]: if takes only" in e for e in errs) and any("if condition must be" in e for e in errs)


def test_events_and_limits():
    errs = applets.validate(minimal(on={"every:0": [], "gpio:50:fall": [], "bad event!": [], "tick": "notalist"}))
    assert any("every:N needs N >= 1" in e for e in errs)
    assert any("pin must be 0-39" in e for e in errs)
    assert any("bad event name 'bad event!'" in e for e in errs)
    assert any("must be a list of actions" in e for e in errs)
    many = minimal(ui=[{"type": "divider"}] * 49)
    assert any("49 widgets" in e for e in applets.validate(many))
    nested = minimal(ui=[{"type": "row", "items": [{"type": "divider"}] * 48}])
    assert any("49 widgets" in e for e in applets.validate(nested))
    timers = minimal(on={f"every:{n}": [] for n in range(1, 10)})
    assert any("9 timers" in e for e in applets.validate(timers))
    gpios = minimal(on={f"gpio:{n}:rise": [] for n in range(5)})
    assert any("5 GPIO watches" in e for e in applets.validate(gpios))
    state = minimal(state={f"k{n}": n for n in range(33)})
    assert any("33 state keys" in e for e in applets.validate(state))
    assert any("no nesting" in e for e in applets.validate(minimal(state={"count": {"a": 1}})))
    assert any("persist: 'zzz'" in e for e in applets.validate(minimal(persist=["zzz"])))
    big = minimal(state={"blob": "x" * 13000})
    assert any("limit is 12288" in e for e in applets.validate(big))


def test_template_brace_balance():
    errs = applets.validate(minimal(ui=[{"type": "label", "text": "${count"}]))
    assert any("unbalanced" in e and "ui[0].text" in e for e in errs)
    assert applets.validate(minimal(ui=[{"type": "label", "text": "${count > 0 ? 'a' : 'b'} and ${time}"}])) == []
    assert applets.validate(minimal(on={"start": [{"set": {"s": "${str(count)}}"}}]})) == []   # extra '}' is literal text


def test_chunker_roundtrip():
    text = "x" * 9001
    parts = applets.chunk_document(text)
    assert "".join(parts) == text and len(parts) == 3 and all(len(p) <= applets.CHUNK_CHARS for p in parts)
    assert applets.chunk_document("") == [""]


def test_install_frames_single_and_chunked():
    small = applets.compact(minimal())
    frames = applets.install_frames("tally", small, True)
    assert len(frames) == 1 and frames[0]["t"] == "applet.install" and frames[0]["json"] == small and frames[0]["open"] is True
    big_doc = minimal(state={"note": "y" * 7000})
    big = applets.compact(big_doc)
    frames = applets.install_frames("tally", big, False)
    assert [f["t"] for f in frames] == ["applet.put", "applet.put", "applet.install"]
    assert "json" not in frames[-1] and frames[-1]["open"] is False
    assert all(protocol.fits(f) for f in frames)
    assert "".join(f["d"] for f in frames[:-1]) == big
    assert [(f["i"], f["n"]) for f in frames[:-1]] == [(0, 2), (1, 2)]
