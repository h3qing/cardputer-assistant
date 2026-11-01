"""DeviceLink.request_reply (docs/11 §6.1): a future that resolves on the matching re, or fails after 5 s.

It is metered by the credit window like request(), and leaves request() / coalescing / nudges unchanged.
"""

from __future__ import annotations

import asyncio
import json
import queue
import time

import pytest

from cpad.device.link import DeviceLink, LinkClosed, LinkTiming, ReplyTimeout
from cpad.device.port_finder import PortInfo
from cpad.protocol import codec
from cpad.protocol import file_messages as f
from cpad.protocol import messages as m

TIMING = LinkTiming(retry_s=0.1, attempts=3, ping_s=30.0, timeout_s=30.0, nudge_s=0.03, reclaim_s=5.0, tick_s=0.01,
                    reply_s=0.3)  # fmt: skip
FID = "K3M9Q2T-00042-0A1C"
REVS = {"projects": 0, "sessions": 0, "queue": 0}


class Port:
    """In-memory device: answers hello; file.get is answered only when `answer` says so."""

    def __init__(self, answer=None) -> None:
        self.writes: list[bytes] = []
        self.inbox: queue.Queue[bytes] = queue.Queue()
        self.in_waiting = 0
        self.seq = 0
        self.answer = answer or (lambda msg: None)

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

    def push(self, msg: dict) -> None:
        self.seq += 1
        self.inbox.put(codec.serialize({"v": 0, "id": f"d{self.seq}", **msg}))

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        if data.startswith(b"{"):
            msg = json.loads(data)
            if msg["t"] == "hello":
                self.push({"t": "hello", "re": msg["id"], "role": "device", "fw": "cpa-fw/0.3.0", "dev": "020000000001",
                           "boot": "3QZK1M7-0000C", "up_ms": 1000, "rst": "poweron", "time": False, "pending": 0,
                           "bat_mv": 4100, "revs": REVS, "files": {"n": 1, "rev": 3, "live": False}})  # fmt: skip
            else:
                reply = self.answer(msg)
                if reply is not None:
                    self.push({"re": msg["id"], **reply})
        return len(data)

    def sent(self, t: str) -> list[dict]:
        return [json.loads(w) for w in self.writes if w.startswith(b"{") and json.loads(w)["t"] == t]


class Handlers:
    def __init__(self) -> None:
        self.pongs: list[int] = []

    def host_hello(self) -> m.HostHello:
        return m.HostHello(app="cpad/0.3.0", agent="ok", caps=m.Caps(spawn=True, reply=False))

    async def on_ready(self, link, hello, reboot) -> None:
        pass

    async def on_request(self, link, decoded) -> None:
        pass

    async def on_pong(self, link, pong) -> None:
        self.pongs.append(link.rx_seq)


def data_for(msg: dict) -> dict:
    return {"t": "file.data", "fid": msg["fid"], "off": msg["off"], "n": 3, "size": 47, "crc": 891568578, "b64": "YWJj"}


async def _until(predicate, timeout: float = 3.0) -> None:
    deadline = time.monotonic() + timeout
    while not predicate():
        assert time.monotonic() < deadline, "condition not met"
        await asyncio.sleep(0.005)


async def _running(port: Port, handlers: Handlers | None = None):
    link = DeviceLink(PortInfo("/dev/cu.fake", "S", 0x303A, 0x1001), handlers or Handlers(),
                      serial_factory=lambda: port, timing=TIMING)  # fmt: skip
    task = asyncio.create_task(link.run())
    await _until(lambda: link.ready)
    return link, task


async def _stop(link: DeviceLink, task: asyncio.Task) -> None:
    link.close("closed")
    await asyncio.wait_for(task, 5)


async def test_resolves_with_the_matching_reply_and_its_receive_order():
    port = Port(answer=data_for)
    link, task = await _running(port)
    try:
        first = link.request_reply(f.FileGet(fid=FID, off=0, len=3))
        second = link.request_reply(f.FileGet(fid=FID, off=3, len=3))
        one, two = await asyncio.wait_for(asyncio.gather(first, second), 2)
        assert (one.msg["t"], one.msg["off"], two.msg["off"]) == ("file.data", 0, 3)
        assert one.seq < two.seq <= link.rx_seq
        assert link.credit_used() == 0
    finally:
        await _stop(link, task)


async def test_error_replies_resolve_instead_of_raising():
    port = Port(answer=lambda msg: {"t": "error", "code": "E_BUSY", "msg": "live"})
    link, task = await _running(port)
    try:
        reply = await asyncio.wait_for(link.request_reply(f.FileList(max=4)), 2)
        assert (reply.msg["t"], reply.msg["code"], reply.msg["msg"]) == ("error", "E_BUSY", "live")
    finally:
        await _stop(link, task)


async def test_times_out_after_reply_s_and_frees_nothing_else():
    port = Port()
    link, task = await _running(port)
    try:
        started = time.monotonic()
        with pytest.raises(ReplyTimeout):
            await asyncio.wait_for(link.request_reply(f.FileGet(fid=FID, off=0, len=1380)), 3)
        assert 0.25 <= time.monotonic() - started < 2.0
        assert len(port.sent("file.get")) == 1
    finally:
        await _stop(link, task)


async def test_close_fails_sent_and_queued_requests():
    port = Port()
    link, task = await _running(port)
    waiting = [link.request_reply(f.FileGet(fid=FID, off=i * 1380, len=1380)) for i in range(4)]
    await _until(lambda: len(port.sent("file.get")) == 4)
    await _stop(link, task)
    for future in waiting:
        with pytest.raises(LinkClosed):
            await future


async def test_credit_window_meters_request_reply():
    port = Port()
    link, task = await _running(port)
    try:
        futures = [link.request_reply(f.FileGet(fid=FID, off=i, len=1380)) for i in range(80)]
        await asyncio.sleep(0.1)
        assert 0 < link.credit_used() <= 4096
        assert len(port.sent("file.get")) < 80
        for future in futures:
            future.cancel()
    finally:
        await _stop(link, task)


async def test_request_keeps_coalescing_and_creates_no_waiter():
    port = Port(answer=lambda msg: {"t": "ack"})
    link, task = await _running(port)
    try:
        link.request(m.TimeSet(epoch_ms=1792440000000, tz_min=0))
        link.request(m.TimeSet(epoch_ms=1792440000001, tz_min=0))
        await _until(lambda: port.sent("time.set"))
        assert len(port.sent("time.set")) <= 2 and link.waiting() == 0
    finally:
        await _stop(link, task)


async def test_pong_handler_sees_a_later_receive_order_than_earlier_replies():
    port = Port(answer=lambda msg: {"t": "error", "code": "E_BUSY", "msg": "live"})
    handlers = Handlers()
    link, task = await _running(port, handlers)
    try:
        busy = await asyncio.wait_for(link.request_reply(f.FileGet(fid=FID, off=0, len=3)), 2)
        port.push({"t": "pong", "re": "h1", "up_ms": 2000, "pending": 0, "revs": REVS,
                   "files": {"n": 1, "rev": 3, "live": False}})  # fmt: skip
        await _until(lambda: handlers.pongs)
        assert handlers.pongs[0] > busy.seq
    finally:
        await _stop(link, task)


async def test_a_request_after_the_link_ended_fails_at_once():
    port = Port()
    link, task = await _running(port)
    await _stop(link, task)
    with pytest.raises(LinkClosed):
        await asyncio.wait_for(link.request_reply(f.FileList(max=4)), 1)
