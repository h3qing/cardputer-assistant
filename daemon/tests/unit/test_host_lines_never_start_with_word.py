"""Every line the host writes starts with '{' or is blank (docs/09 §6, protocol/README rule 13).

The Launcher's serial console is up during its splash screen and parses every incoming line as a
command; a line starting with a letter could be a real one (e.g. `flash overwrite`). So:
  - every host-sent message type serializes to exactly one line starting with '{', whatever its text;
  - DeviceLink, the only writer of the port, writes nothing else but the blank nudge line.
"""

from __future__ import annotations

import asyncio
import json
import queue
import time
from dataclasses import replace

from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.dest import snapshot as ds
from cpad.device.link import DeviceLink, LinkTiming
from cpad.device.port_finder import PortInfo
from cpad.protocol import codec
from cpad.protocol import file_messages as f
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as r
from tests.support.protocol_files import PROTOCOL

TIMING = LinkTiming(retry_s=0.1, attempts=3, ping_s=0.15, timeout_s=5.0, nudge_s=0.03, reclaim_s=0.4, tick_s=0.01)
DEBUG_REQUESTS = ({"t": "debug.key", "keys": ["a", "fn"], "hold_ms": 50}, {"t": "debug.state"}, {"t": "debug.reboot"})
DEBUG_REQUESTS += ({"t": "debug.rec", "seg_ms": 30000},)  # v0.3 test hook; additive G9 exception (docs/12 M3-B)
DEBUG_REQUESTS += ({"t": "debug.mic", "pga": 10, "vol": 191, "mag": 2},)  # v0.3.1 test hook (HIL mic_cal)
DEBUG_REQUESTS += ({"t": "debug.codec", "warm": False},)  # v0.3.2 test hook (HIL rec_cold_start, issue #31)
DEBUG_REQUESTS += ({"t": "debug.lang", "lang": "en"},)  # v0.4.3 test hook (HIL settings, docs/21 §7)
words = st.text(alphabet=st.sampled_from('flash overwrite\n\r\t{}"\\ab会议-/'), min_size=1, max_size=40)


def host_line_ok(line: bytes) -> bool:
    """One complete line that is blank or a JSON object; never a word the Launcher could run."""
    body = line.removesuffix(b"\n")
    return line.endswith(b"\n") and b"\n" not in body and (body == b"" or body.startswith(b"{"))


def _host_sent_types() -> set[str]:
    schemas = (PROTOCOL / "schema" / "v0").glob("*.schema.json")
    docs = (json.loads(p.read_text(encoding="utf-8")) for p in schemas if p.name != "common.schema.json")
    return {d["title"] for d in docs if "h2d" in d["x-dir"]}


def _host_messages(text: str) -> tuple:
    project = m.ProjectRow(slot=1, key="cpa", name=text, run=0, need=0, queued=0, ok=True)
    session = m.SessionRow(sid="8d3e7a19", slot=1, name=text, kind="tty", st="idle", need=False, age_s=1, short="x")
    return (
        m.HostHello(app="cpad/0.2.0", agent="ok", caps=m.Caps(spawn=True, reply=False)),
        m.Ping(),
        m.TimeSet(epoch_ms=1792440000000, tz_min=-420),
        m.ProjectsSnapshot(rev=1, projects=(project,), other=m.Other(run=0, n=0)),
        m.SessionsSnapshot(rev=1, total=1, sessions=(session,)),
        m.QueueSnapshot(rev=1, items=(m.QueueRow(qid="q1", slot=1, text=text, intent="todo"),)),
        m.Ack(re="d1"),
        m.ErrorMsg(re="d1", code="E_BAD_FIELD", field="body.text", msg="flash overwrite"),
        m.OutboxAck(re="3QZK1M7-0000C-0001", status="rejected", code="E_STORAGE"),
        m.AgentResult(re="d2", ok=True, name=text, short="a1b2c3d4"),
        f.FileList(max=4),
        f.FileGet(fid="3QZK1M7-0000C-0001", off=0, len=1380),
        f.FileDel(fid="3QZK1M7-0000C-0001", size=44),
        r.RecItems(re="d3", items=(r.RecRow(rec="3QZK1M7-00009-0001", mode="memo", st="done", title=text),), next="t1"),
        ds.DestsSnapshot(rev=1, dests=(ds.DestRow(id="clip", slot=5, label=text, kind="clip", key="c", fl="l"),)),
    )


def _wire(msg) -> dict:
    return m.to_wire(replace(msg, id="h1"))


def test_the_checker_bites():
    assert host_line_ok(b'{"v":0}\n') and host_line_ok(b"\n")
    for bad in (b"flash overwrite cpa 1\n", b" {}\n", b"{}", b'{"a":1}\n{"b":2}\n', b"\r\n", b"ls\n"):
        assert not host_line_ok(bad), bad


def test_samples_cover_every_host_sent_type():
    covered = {_wire(msg)["t"] for msg in _host_messages("x")} | {d["t"] for d in DEBUG_REQUESTS}
    assert covered == _host_sent_types()


@settings(max_examples=40, deadline=None)
@given(text=words)
def test_every_host_message_is_one_line_starting_with_a_brace(text):
    wires = [_wire(msg) for msg in _host_messages(text)] + [{"v": 0, "id": "h1", **d} for d in DEBUG_REQUESTS]
    for wire in wires:
        line = codec.serialize(wire)
        assert host_line_ok(line), line
        assert not line[:1].isalpha()


# --- the link's write path -------------------------------------------------------------------


class ScriptedPort:
    """In-memory port that records every host write and answers like cpa-fw, except that it
    ignores the first hello (hello retry) and never answers time.set (the host must nudge)."""

    def __init__(self) -> None:
        self.writes: list[bytes] = []
        self.inbox: queue.Queue[bytes] = queue.Queue()
        self.in_waiting = 0
        self.hellos = 0
        self.seq = 0

    def open(self) -> None:
        pass

    def close(self) -> None:
        pass

    def cancel_read(self) -> None:
        pass

    def read(self, _size: int) -> bytes:
        try:
            return self.inbox.get(timeout=0.01)
        except queue.Empty:
            return b""

    def push(self, data: bytes) -> None:
        self.inbox.put(data)

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        if data.startswith(b"{"):
            self._answer(json.loads(data))
        return len(data)

    def _reply(self, re: str, t: str, **fields) -> None:
        self.seq += 1
        self.push(codec.serialize({"v": 0, "t": t, "id": f"d{self.seq}", "re": re, **fields}))

    def _answer(self, msg: dict) -> None:
        t = msg["t"]
        if t == "hello":
            self.hellos += 1
            if self.hellos > 1:
                self._reply(msg["id"], "hello", role="device", fw="cpa-fw/0.2.0", dev="020000000001",
                            boot="3QZK1M7-0000C", up_ms=1000 + self.seq, rst="software", time=False, pending=1,
                            bat_mv=4150, revs={"projects": 0, "sessions": 0, "queue": 0}, store="sd",
                            launcher=True)  # fmt: skip
        elif t == "ping":
            self._reply(msg["id"], "pong", up_ms=2000 + self.seq, pending=0,
                        revs={"projects": 0, "sessions": 0, "queue": 0})  # fmt: skip
        elif t != "time.set" and "re" not in msg:
            self._reply(msg["id"], "debug.state" if t == "debug.state" else "ack", **_state_fields(t))


def _state_fields(t: str) -> dict:
    if t != "debug.state":
        return {}
    return {"up_ms": 1, "link": "Ready", "mod": "board", "overlay": "none", "revs": {"projects": 0, "sessions": 0,
            "queue": 0}, "outbox": {"pending": 0, "inflight": 0, "bad": 0}, "need": 0, "projects": []}  # fmt: skip


class Handlers:
    """Plays the daemon: resyncs on ready and answers device requests (ok or refused)."""

    def __init__(self) -> None:
        self.replied: list[str] = []

    def host_hello(self) -> m.HostHello:
        return m.HostHello(app="cpad/0.2.0", agent="ok", caps=m.Caps(spawn=True, reply=False))

    async def on_ready(self, link: DeviceLink, hello: m.DeviceHello, reboot: bool) -> None:
        for msg in _host_messages("flash overwrite cpa 1")[2:6]:
            link.request(msg)
        for raw in DEBUG_REQUESTS[:2]:
            link.request_raw(raw)

    async def on_request(self, link: DeviceLink, decoded: codec.Decoded) -> None:
        if decoded.ok:
            link.reply(m.OutboxAck(re=decoded.msg["id"], status="stored"))
        else:
            link.reply(m.ErrorMsg(re=decoded.reply_id, code=decoded.outcome, field=decoded.fields[0]))
        self.replied.append(decoded.reply_id)

    async def on_pong(self, link: DeviceLink, pong: m.Pong) -> None:
        pass


async def _until(predicate, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while not predicate():
        assert time.monotonic() < deadline, "condition not met"
        await asyncio.sleep(0.01)


def _device_requests() -> tuple[bytes, ...]:
    item = {"v": 0, "t": "outbox.item", "id": "3QZK1M7-0000C-0001", "kind": "note", "at": {"up_ms": 1},
            "body": {"text": "flash overwrite", "project": "cpa"}}  # fmt: skip
    bad = {"v": 0, "t": "queue.done", "id": "d90", "qid": "not a qid"}
    noise = b"Press the button to enter the Launcher!\r\n"
    return noise, codec.serialize(item), codec.serialize(bad)


async def test_the_link_writes_only_json_lines_and_blank_nudges():
    port, handlers = ScriptedPort(), Handlers()
    link = DeviceLink(PortInfo("/dev/cu.fake", "SER1", 0x303A, 0x1001), handlers, serial_factory=lambda: port,
                      timing=TIMING)  # fmt: skip
    task = asyncio.create_task(link.run())
    try:
        await _until(lambda: link.ready)
        for data in _device_requests():
            port.push(data)
        await _until(lambda: len(handlers.replied) == 2 and b"\n" in port.writes)
        await _until(lambda: sum(w.startswith(b'{"v":0,"t":"ping"') for w in port.writes) >= 2)
    finally:
        link.close("closed")
        outcome = await asyncio.wait_for(task, 5)
    assert outcome.reason == "closed" and port.hellos >= 2
    assert [w for w in port.writes if not host_line_ok(w)] == []
    assert not any(line[:1].isalpha() for line in b"".join(port.writes).split(b"\n"))
    sent = {json.loads(w)["t"] for w in port.writes if w != b"\n"}
    assert {"hello", "ping", "time.set", "projects.snapshot", "debug.key", "outbox.ack", "error"} <= sent
