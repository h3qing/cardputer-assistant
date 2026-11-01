"""SimLink: the part of DeviceLink that RecPuller uses (request_reply, rx_seq), over FakeFiles in memory.

Replies come back on the next loop iteration; `drop` makes chosen requests time out after reply_s.
Every line is traced so a test can turn the exchange into a protocol flow and run the reference
host-invariant checker (protocol/tools/file_flows.py) over it.
"""

from __future__ import annotations

import asyncio
import time
from collections.abc import Callable

from cpad.device.link import LinkClosed, Reply, ReplyTimeout
from cpad.protocol import messages as m
from tests.support.fake_files import FakeFiles

REVS = {"projects": 0, "sessions": 0, "queue": 0}


def _consume(future: asyncio.Future) -> None:
    if not future.cancelled():
        future.exception()


class SimLink:
    def __init__(self, files: FakeFiles, *, reply_s: float = 0.2, drop: Callable[[dict], bool] | None = None,
                 on_send: Callable[[dict], None] | None = None):  # fmt: skip
        self.files = files
        self.on_send = on_send
        self.reply_s = reply_s
        self.drop = drop
        self.rx_seq = 0
        self.trace: list[tuple[float, str, dict]] = []
        self.listeners: list[Callable] = []
        self.closed = False
        self._pending: set[asyncio.Future] = set()
        self._ids = 0
        self._t0 = time.monotonic()

    def _now(self) -> float:
        return round((time.monotonic() - self._t0) * 1000, 3)

    def request_reply(self, msg) -> asyncio.Future:
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        future.add_done_callback(_consume)
        self._ids += 1
        wire = {**m.to_wire(msg), "id": f"h{self._ids}"}
        self.trace.append((self._now(), "host", wire))
        if self.on_send is not None:
            self.on_send(wire)
        self._pending.add(future)
        if self.closed:
            self._fail(future, LinkClosed("closed"))
        elif self.drop is not None and self.drop(wire):
            loop.call_later(self.reply_s, self._fail, future, ReplyTimeout("no reply"))
        else:
            loop.call_soon(self._answer, future, wire)
        return future

    def _fail(self, future: asyncio.Future, exc: Exception) -> None:
        self._pending.discard(future)
        if not future.done():
            future.set_exception(exc)

    def _answer(self, future: asyncio.Future, wire: dict) -> None:
        if self.closed or future.done():
            self._fail(future, LinkClosed("closed"))
            return
        reply = {"v": 0, "id": f"d{self._ids}{len(self.trace)}", "re": wire["id"], **self.files.handle(wire)}
        self.rx_seq += 1
        self.trace.append((self._now(), "device", reply))
        self._pending.discard(future)
        future.set_result(Reply(reply, self.rx_seq))

    def pong(self, up_ms: int = 900000) -> None:
        """A pong with the device's current files status, delivered to every listener (RecPuller.observe)."""
        self.rx_seq += 1
        files = self.files.status()
        self._ids += 1
        self.trace.append((self._now(), "host", {"v": 0, "t": "ping", "id": f"h{self._ids}"}))
        self.trace.append(
            (
                self._now(),
                "device",
                {
                    "v": 0,
                    "t": "pong",
                    "id": f"p{self._ids}",
                    "re": f"h{self._ids}",
                    "up_ms": up_ms,
                    "pending": 0,
                    "revs": REVS,
                    "files": files,
                },
            )
        )
        for listener in self.listeners:
            listener(m.from_wire(self.trace[-1][2]))

    def close(self) -> None:
        self.closed = True
        for future in list(self._pending):
            self._fail(future, LinkClosed("closed"))

    def host_sent(self, t: str) -> list[dict]:
        return [msg for _, side, msg in self.trace if side == "host" and msg["t"] == t]

    def as_flow(self, name: str) -> dict:
        """The trace as a flow vector; step k happens at t = k ms, so the order is exact."""
        steps = [{"t": k, "from": side, "msg": msg} for k, (_, side, msg) in enumerate(self.trace)]
        return {"name": name, "steps": steps}
