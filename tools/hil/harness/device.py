"""Driving the test firmware through the daemon's own link (I/O shell).

debug.* go out with DeviceLink.request_raw (the daemon's HIL seam: same ids, credit window and
nudges as every other host message); the device's replies are read back from the tap.
"""

from __future__ import annotations

import asyncio
import time
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from harness import keys as k
from harness import ui
from harness.rig import Rig, until
from harness.wire import Rec, find, reply_to

REPLY_TIMEOUT_S = 5.0
NOT_READY_RETRIES = 3
REHANDSHAKE_S = 8.0  # cpad: hello every 2 s, 3 attempts


class DeviceError(RuntimeError):
    """The device refused or never answered a debug request."""


@dataclass(frozen=True, slots=True)
class Sample:
    """A debug.state reply and when the host sent the request / received the reply."""

    sent_at: float
    recv_at: float
    msg: Mapping[str, Any]


def slot_row(state: Mapping[str, Any], slot: int) -> dict[str, int] | None:
    return next((dict(p) for p in state.get("projects", []) if p.get("slot") == slot), None)


def _same(wire: Mapping[str, Any]) -> Callable[[Mapping[str, Any]], bool]:
    return lambda msg: all(msg.get(key) == value for key, value in wire.items())


class Device:
    def __init__(self, rig: Rig):
        self.rig = rig
        self.tap = rig.tap

    async def _sent(self, mark: int, wire: Mapping[str, Any], nth: int = 0) -> tuple[int, Rec]:
        def located() -> tuple[int, Rec] | None:
            recs, start = self.tap.since(mark), 0
            for _ in range(nth + 1):
                i = find(recs, "tx", wire["t"], start=start, where=_same(wire))
                if i is None:
                    return None
                start = i + 1
            return mark + i, recs[i]

        return await until(located, REPLY_TIMEOUT_S, 0.01, what=f"{wire['t']} written")

    async def _reply(self, index: int, sent: Rec, timeout: float) -> Rec:
        msg_id = sent.msg["id"]
        return await until(lambda: reply_to(self.tap.since(index), msg_id), timeout, 0.01, what=f"reply to {msg_id}")

    async def _request_once(self, wire: Mapping[str, Any], timeout: float) -> tuple[Rec, Rec]:
        link = await self.rig.ready()
        mark = self.tap.size()
        link.request_raw(dict(wire))
        index, sent = await self._sent(mark, wire)
        return sent, await self._reply(index, sent, timeout)

    async def request(self, wire: Mapping[str, Any], timeout: float = REPLY_TIMEOUT_S) -> tuple[Rec, Rec]:
        """Send and wait for the reply. E_NOT_READY means the device fell out of Ready under the
        host; cpad re-handshakes by itself on that reply, so the request is sent again once the
        link is Ready. Every such reply stays in the tap, and the runner reports how many."""
        for _ in range(NOT_READY_RETRIES):
            mark = self.tap.size()
            sent, reply = await self._request_once(wire, timeout)
            if reply.msg.get("code") != "E_NOT_READY":
                return sent, reply
            await until(lambda m=mark: find(self.tap.since(m), "rx", "hello"), REHANDSHAKE_S, what="re-handshake")
        return sent, reply

    async def press(self, press: k.Press, pace: bool = True) -> Rec:
        sent, reply = await self.request(k.wire(press))
        if reply.msg.get("t") != "ack":
            raise DeviceError(f"debug.key {press.keys} answered {reply.msg.get('t')} {reply.msg.get('code')}")
        if pace:  # one press in the device's inject queue at a time
            await asyncio.sleep(max(0.0, k.duration_ms(press) / 1000 + 0.01 - (reply.t - sent.t)))
        return sent

    async def presses(self, seq: Sequence[k.Press]) -> float:
        """Press seq one by one; returns the host time just before the last press was sent."""
        last = time.monotonic()
        for press in seq:
            last = time.monotonic()
            await self.press(press)
        return last

    async def preload(self, plan: Sequence[k.Press]) -> float:
        """Queue the whole plan at once (it must fit the inject queue); returns when every press is acked."""
        if len(plan) > k.INJECT_QUEUE_MAX:
            raise ValueError("plan larger than the device inject queue")
        link = await self.rig.ready()
        mark = self.tap.size()
        wires = [k.wire(p) for p in plan]
        for wire in wires:
            link.request_raw(wire)
        for n, wire in enumerate(wires):
            nth = sum(1 for w in wires[:n] if w == wire)
            index, sent = await self._sent(mark, wire, nth)
            reply = await self._reply(index, sent, REPLY_TIMEOUT_S)
            if reply.msg.get("t") != "ack":
                raise DeviceError(f"preloaded debug.key {wire['keys']} answered {reply.msg.get('t')}")
        return time.monotonic()

    async def state(self) -> Sample:
        sent, reply = await self.request({"t": "debug.state"})
        if reply.msg.get("t") != "debug.state":
            raise DeviceError(f"debug.state answered {reply.msg.get('t')} {reply.msg.get('code')}")
        return Sample(sent.t, reply.t, reply.msg)

    async def wait_state(self, predicate: Callable[[Mapping[str, Any]], bool], timeout: float,
                         interval: float = 0.1, what: str = "device state") -> Sample:  # fmt: skip
        deadline = time.monotonic() + timeout
        while True:
            sample = await self.state()
            if predicate(sample.msg):
                return sample
            if time.monotonic() >= deadline:
                raise TimeoutError(f"{what} not reached within {timeout:g} s (last: {dict(sample.msg)})")
            await asyncio.sleep(interval)

    async def home(self) -> None:
        """Unlock, close the Tab menu and any overlay (Fn+`; a draft is discarded with ui.COMPOSE_CLOSE, never
        saved), then Opt+H to the root page: the home launcher since v0.4.0 (docs/13 §5.5)."""
        for _ in range(6):
            step = ui.unwind_step((await self.state()).msg)
            if not step:
                break
            await self.presses(step)
        await self.press(ui.OPT_H)
        await self.wait_state(ui.View(ui.ROOT).met, 3, what="home")

    async def reboot(self) -> Rec:
        sent, reply = await self.request({"t": "debug.reboot"})
        if reply.msg.get("t") != "ack":
            raise DeviceError(f"debug.reboot answered {reply.msg.get('t')}")
        return reply
