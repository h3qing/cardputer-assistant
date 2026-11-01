"""Frozen dataclass messages: to_wire / from_wire round trips (05 §6.5, examples from §6.6)."""

from __future__ import annotations

import dataclasses

import pytest

from cpad.protocol import codec
from cpad.protocol import messages as m

DEVICE_HELLO = {
    "v": 0, "t": "hello", "id": "d1", "re": "h1", "role": "device", "fw": "cpa-fw/0.1.0", "dev": "020000000001",
    "boot": "3QZK1M7-0000C", "up_ms": 53211, "rst": "poweron", "time": False, "pending": 3, "bat_mv": 4150,
    "revs": {"projects": 0, "sessions": 0, "queue": 0},
}  # fmt: skip


def test_host_hello_matches_the_spec_example():
    hello = m.HostHello(app="cpad/0.1.0", agent="ok", caps=m.Caps(spawn=True, reply=False), id="h1")
    assert m.to_wire(hello) == {
        "v": 0, "t": "hello", "id": "h1", "role": "host", "app": "cpad/0.1.0", "agent": "ok",
        "caps": {"spawn": True, "reply": False},
    }  # fmt: skip


def test_v041_host_hello_carries_caps_and_host_identity():
    caps = m.Caps(spawn=True, reply=False, recs=True, todo=True)
    hello = m.HostHello(app="cpad/0.4.1", agent="ok", caps=caps, host_id="7f3a9c1e5b2d4a60", host_label="Studio Mac",
                        id="h1")  # fmt: skip
    wire = m.to_wire(hello)
    assert wire == {
        "v": 0, "t": "hello", "id": "h1", "role": "host", "app": "cpad/0.4.1", "agent": "ok",
        "caps": {"spawn": True, "reply": False, "recs": True, "todo": True},
        "host_id": "7f3a9c1e5b2d4a60", "host_label": "Studio Mac",
    }  # fmt: skip
    assert codec.decode_line(codec.encode(wire)).ok


def test_v041_hello_fields_default_to_absent():
    wire = m.to_wire(m.HostHello(app="cpad/0.3.1", agent="ok", caps=m.Caps(spawn=True, reply=False), id="h1"))
    assert set(wire) == {"v", "t", "id", "role", "app", "agent", "caps"} and set(wire["caps"]) == {"spawn", "reply"}


def test_messages_are_frozen():
    ping = m.Ping(id="h1")
    with pytest.raises(dataclasses.FrozenInstanceError):
        ping.id = "h2"  # type: ignore[misc]


def test_optional_fields_are_omitted_when_none():
    result = m.AgentResult(re="d7", ok=True, name="cpa/write-tests", short="a1b2c3d4", id="h7")
    assert m.to_wire(result) == {
        "v": 0, "t": "agent.result", "id": "h7", "re": "d7", "ok": True, "name": "cpa/write-tests", "short": "a1b2c3d4",
    }  # fmt: skip
    assert "code" not in m.to_wire(m.OutboxAck(re="3QZK1M7-0000C-0003", status="stored", id="h6"))


def test_snapshots_serialize_rows_as_lists():
    snap = m.SessionsSnapshot(
        rev=17,
        total=1,
        sessions=(m.SessionRow(sid="8d3e7a19", slot=1, name="架构设计", kind="tty", st="waiting", need=True, age_s=5),),
        id="h4",
    )
    wire = m.to_wire(snap)
    assert wire["sessions"] == [
        {"sid": "8d3e7a19", "slot": 1, "name": "架构设计", "kind": "tty", "st": "waiting", "need": True, "age_s": 5}
    ]
    assert codec.decode_line(codec.encode(wire)).ok


def test_device_hello_from_wire():
    hello = m.from_wire(DEVICE_HELLO)
    assert isinstance(hello, m.DeviceHello)
    assert (hello.boot, hello.up_ms, hello.revs) == ("3QZK1M7-0000C", 53211, m.Revs(0, 0, 0))


def test_outbox_items_from_wire():
    note = m.from_wire(
        {
            "v": 0,
            "t": "outbox.item",
            "id": "3QZK1M7-0000C-0003",
            "kind": "note",
            "at": {"up_ms": 41000, "wall_ms": None},
            "body": {"text": "idea", "project": "cpa"},
        }  # fmt: skip
    )
    assert note.body == m.NoteBody(text="idea", project="cpa") and note.at == m.At(41000, None)
    queue = m.from_wire(
        {
            "v": 0,
            "t": "outbox.item",
            "id": "3QZK1M7-0000C-0004",
            "kind": "queue",
            "at": {"up_ms": 1},
            "body": {"text": "fix", "project": "cpa", "intent": "feedback", "sid": "5c2e8a14", "uncertain": True},
        }  # fmt: skip
    )
    assert queue.body == m.QueueBody(text="fix", project="cpa", intent="feedback", sid="5c2e8a14", uncertain=True)


def test_note_dest_from_wire_and_absent_by_default():
    wire = {"v": 0, "t": "outbox.item", "id": "3QZK1M7-0000C-0005", "kind": "note", "at": {"up_ms": 98000},
            "body": {"text": "call the plumber", "project": "cpa", "dest": "todo"}}  # fmt: skip
    assert m.from_wire(wire).body == m.NoteBody(text="call the plumber", project="cpa", dest="todo")
    plain = m.from_wire({**wire, "body": {"text": "call the plumber"}})
    assert plain.body.dest is None


@pytest.mark.parametrize(
    ("wire", "cls"),
    [
        ({"v": 0, "t": "pong", "id": "d2", "re": "h2", "up_ms": 9, "pending": 0,
          "revs": {"projects": 1, "sessions": 2, "queue": 3}}, m.Pong),
        ({"v": 0, "t": "ack", "id": "d3", "re": "h3"}, m.Ack),
        ({"v": 0, "t": "error", "id": "d4", "re": "h4", "code": "E_NOT_READY"}, m.ErrorMsg),
        ({"v": 0, "t": "agent.spawn", "id": "d5", "project": "cpa", "text": "go"}, m.AgentSpawn),
        ({"v": 0, "t": "agent.reply", "id": "d6", "sid": "5c2e8a14", "text": "ok"}, m.AgentReply),
        ({"v": 0, "t": "queue.done", "id": "d7", "qid": "q1"}, m.QueueDone),
        ({"v": 0, "t": "inbox.dismiss", "id": "d8", "sid": "5c2e8a14"}, m.InboxDismiss),
        ({"v": 0, "t": "debug.state", "id": "h9"}, m.Unhandled),
    ],
)  # fmt: skip
def test_from_wire_types(wire, cls):
    assert isinstance(m.from_wire(wire), cls)


@pytest.mark.parametrize(
    "msg",
    [
        m.Ping(id="h1"),
        m.TimeSet(epoch_ms=1792440000000, tz_min=-420, id="h2"),
        m.ProjectsSnapshot(
            rev=42,
            projects=(m.ProjectRow(slot=1, key="cpa", name="Cardputer 助手", run=1, need=1, queued=2, ok=True),),
            other=m.Other(run=0, n=1),
            id="h3",
        ),
        m.QueueSnapshot(rev=9, items=(m.QueueRow(qid="q17", slot=1, text="add test", intent="todo"),), id="h5"),
        m.Ack(re="d9", id="h9"),
        m.ErrorMsg(re="d12", code="E_UNKNOWN_ITEM", id="h12"),
        m.OutboxAck(re="3QZK1M7-0000C-0003", status="rejected", code="E_BAD_FIELD", id="h6"),
        m.AgentResult(re="d8", ok=False, code="E_NOT_REPLYABLE", id="h8"),
    ],
)
def test_host_messages_are_valid_lines(msg):
    assert codec.decode_line(codec.encode(m.to_wire(msg))).ok
