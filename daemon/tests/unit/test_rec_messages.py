"""v0.3 recording transfer messages (docs/11 §5): files on hello/pong, file.* dataclasses, debug.rec.

v0.4.1 (docs/13 §8, §12.2): dest on file items; the recordings app's rec.list / rec.items / rec.act.
"""

from __future__ import annotations

import json

import pytest

from cpad.protocol import codec
from cpad.protocol import file_messages as f
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as r
from tests.support.protocol_files import VECTORS

HELLO = {"v": 0, "t": "hello", "id": "d1", "re": "h1", "role": "device", "fw": "cpa-fw/0.3.0", "dev": "020000000001",
         "boot": "3QZK1M7-0000C", "up_ms": 5000, "rst": "poweron", "time": False, "pending": 0, "bat_mv": 4100,
         "revs": {"projects": 0, "sessions": 0, "queue": 0}}  # fmt: skip
ITEM = {
    "id": "K3M9Q2T-00042-0A1C",
    "kind": "rec",
    "size": 384044,
    "sha256": "a" * 64,
    "rec": "K3M9Q2T-00042-0A1C",
    "seg": 0,
    "last": True,
    "mode": "memo",
    "dur_ms": 12000,
    "at": {"up_ms": 435000, "wall_ms": None},
    "project": "cpa",
    "lead_ms": 200,
    "stars": [100, 200],
    "consent": "ack",
    "rcv": False,
    "x_future": 1,
}


def test_hello_and_pong_carry_optional_files():
    hello = m.from_wire({**HELLO, "files": {"n": 2, "rev": 17, "live": False}})
    assert hello.files == f.Files(n=2, rev=17, live=False)
    assert m.from_wire(HELLO).files is None
    assert m.from_wire({**HELLO, "files": None}).files is None
    pong = m.from_wire(
        {
            "v": 0,
            "t": "pong",
            "id": "d2",
            "re": "h2",
            "up_ms": 1,
            "pending": 0,
            "revs": {"projects": 0, "sessions": 0, "queue": 0},
            "files": {"n": 0, "rev": 0, "live": True},
        }
    )
    assert pong.files == f.Files(0, 0, True)


def test_file_items_parse_to_frozen_items_and_ignore_unknown_fields():
    msg = m.from_wire({"v": 0, "t": "file.items", "id": "d3", "re": "h3", "items": [ITEM], "more": False})
    assert isinstance(msg, f.FileItems) and msg.more is False
    item = msg.items[0]
    assert (item.id, item.seg, item.last, item.mode, item.size, item.sha256) == (ITEM["id"], 0, True, "memo", 384044,
                                                                                 "a" * 64)  # fmt: skip
    assert item.at == m.At(435000, None) and item.stars == (100, 200) and item.consent == "ack"
    bare = m.from_wire({"v": 0, "t": "file.items", "id": "d4", "re": "h4", "more": True,
                        "items": [{**ITEM, "at": None, "stars": None, "sha256": None, "rcv": True}]})  # fmt: skip
    assert bare.items[0].at is None and bare.items[0].stars == () and bare.items[0].rcv is True


def test_file_data_parses_with_and_without_payload():
    data = m.from_wire({"v": 0, "t": "file.data", "id": "d5", "re": "h5", "fid": ITEM["id"], "off": 0, "n": 3,
                        "size": 47, "crc": 1, "b64": "YWJj"})  # fmt: skip
    assert isinstance(data, f.FileData) and data.payload() == b"abc"
    empty = m.from_wire({"v": 0, "t": "file.data", "id": "d6", "re": "h6", "fid": ITEM["id"], "off": 47, "n": 0,
                         "size": 47, "crc": 0})  # fmt: skip
    assert empty.payload() == b""


def test_host_file_requests_render_valid_wire_lines():
    wires = [
        m.to_wire(f.FileList(max=4, id="h1")),
        m.to_wire(f.FileList(after=ITEM["id"], max=4, id="h2")),
        m.to_wire(f.FileGet(fid=ITEM["id"], off=1380, len=1380, id="h3")),
        m.to_wire(f.FileDel(fid=ITEM["id"], size=384044, sha256="a" * 64, id="h4")),
        m.to_wire(f.FileDel(fid=ITEM["id"], size=384044, id="h5")),
        m.to_wire(f.DebugRec(seg_ms=30000, id="h6")),
    ]
    assert wires[0] == {"v": 0, "t": "file.list", "id": "h1", "max": 4}
    assert wires[4] == {"v": 0, "t": "file.del", "id": "h5", "fid": ITEM["id"], "size": 384044}
    for wire in wires:
        assert codec.decode_line(codec.encode(wire)).ok, wire


def test_host_file_requests_are_single_lines_starting_with_a_brace():
    """docs/09 §6: the Launcher console runs lines that start with a word; file.* never does."""
    for msg in (f.FileList(), f.FileGet(fid=ITEM["id"], off=0, len=1), f.FileDel(fid=ITEM["id"], size=44),
                f.DebugRec(seg_ms=10000)):  # fmt: skip
        line = codec.encode(m.to_wire(msg))
        assert line.startswith(b"{") and line.endswith(b"\n") and line.count(b"\n") == 1


def test_file_item_dest_is_parsed_and_absent_by_default():
    page = {"v": 0, "t": "file.items", "id": "d3", "re": "h3", "more": False}
    assert m.from_wire({**page, "items": [{**ITEM, "dest": "todo"}]}).items[0].dest == "todo"
    assert m.from_wire({**page, "items": [ITEM]}).items[0].dest is None


# --- v0.4.1 recordings app ------------------------------------------------------------------------

SID = "3QZK1M7-00009-0001"
ROW = r.RecRow(rec=SID, mode="meeting", st="done", wall_ms=1792440000000, dur_ms=2530000, project="cpa",
               starred=True, marks=2, title="客户同步：定价和交付", todo_n=3)  # fmt: skip


def test_rec_list_parses_to_a_frozen_request():
    first = m.from_wire({"v": 0, "t": "rec.list", "id": "d23", "filter": "all", "max": 6, "x_future": 1})
    assert first == r.RecList(id="d23", filter="all", max=6)
    again = m.from_wire({"v": 0, "t": "rec.list", "id": "d25", "filter": "starred", "max": 6, "cursor": "t17-a1"})
    assert again.cursor == "t17-a1" and again.filter == "starred"


@pytest.mark.parametrize(("extra", "project"), [({"op": "hide"}, None), ({"op": "tag", "project": "cpa"}, "cpa"),
                                                ({"op": "tag", "project": None}, None)])  # fmt: skip
def test_rec_act_parses_to_a_frozen_request(extra, project):
    act = m.from_wire({"v": 0, "t": "rec.act", "id": "d27", "rec": SID, **extra})
    assert act == r.RecAct(id="d27", rec=SID, op=extra["op"], project=project)


def test_rec_items_renders_the_spec_example():
    wire = m.to_wire(r.RecItems(re="d23", items=(ROW,), next="t1792440000000-a1", id="h24"))
    assert wire == {
        "v": 0, "t": "rec.items", "id": "h24", "re": "d23", "next": "t1792440000000-a1",
        "items": [{"rec": SID, "mode": "meeting", "st": "done", "wall_ms": 1792440000000, "dur_ms": 2530000,
                   "project": "cpa", "starred": True, "marks": 2, "title": "客户同步：定价和交付", "todo_n": 3}],
    }  # fmt: skip
    assert codec.decode_line(codec.encode(wire)).ok


def test_rec_items_omits_unknown_fields_and_keeps_an_empty_page():
    bare = m.to_wire(r.RecItems(re="d23", items=(r.RecRow(rec=SID, mode="memo", st="transcript"),), id="h2"))
    assert bare == {"v": 0, "t": "rec.items", "id": "h2", "re": "d23",
                    "items": [{"rec": SID, "mode": "memo", "st": "transcript"}]}  # fmt: skip
    empty = m.to_wire(r.RecItems(re="d23", items=(), id="h3"))
    assert empty["items"] == [] and "next" not in empty and codec.decode_line(codec.encode(empty)).ok


def _without_nulls(value):
    if isinstance(value, dict):
        return {k: _without_nulls(v) for k, v in value.items() if v is not None}
    if isinstance(value, list):
        return [_without_nulls(v) for v in value]
    return value


def _valid_rec_vectors() -> list[dict]:
    docs = (json.loads((VECTORS / "messages" / f"{t}.json").read_text(encoding="utf-8"))
            for t in ("rec.list", "rec.items", "rec.act"))  # fmt: skip
    return [case["msg"] for doc in docs for case in doc["valid"]]


@pytest.mark.parametrize("wire", _valid_rec_vectors(), ids=lambda w: f"{w['t']}:{w['id']}")
def test_every_valid_rec_vector_round_trips(wire):
    """encode(decode(x)) == x, null and absent being the same thing."""
    msg = m.from_wire(wire)
    assert isinstance(msg, r.RecList | r.RecAct | r.RecItems)
    assert m.to_wire(msg) == _without_nulls(wire)
