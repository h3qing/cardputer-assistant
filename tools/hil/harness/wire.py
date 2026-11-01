"""Pure queries over the serial tap's record of both directions.

rx = device -> host, tx = host -> device; times are the host's monotonic clock. A host-initiated
message (no `re`) owes exactly one device reply (05 §6.3 rule 2), so pairing tx requests with rx
replies gives every round trip of a run, and a request without a timely reply is a TX stall.
"""

from __future__ import annotations

import json
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, replace
from typing import Any

Msg = Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class Rec:
    t: float
    dir: str
    raw: bytes
    msg: Msg | None


@dataclass(frozen=True, slots=True)
class Exchange:
    id: str
    t: str
    sent_at: float
    reply_at: float | None
    reply: Msg | None


def decode(raw: bytes) -> dict[str, Any] | None:
    body = raw.rstrip(b"\r\n")
    if not body.startswith(b"{"):
        return None
    try:
        obj = json.loads(body.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None
    return obj if isinstance(obj, dict) else None


def make_rec(t: float, direction: str, raw: bytes) -> Rec:
    return Rec(t, direction, raw, decode(raw))


def find(
    recs: Sequence[Rec], direction: str, t: str, start: int = 0, where: Callable[[Msg], bool] | None = None
) -> int | None:
    for i in range(start, len(recs)):
        r = recs[i]
        if r.dir == direction and r.msg is not None and r.msg.get("t") == t and (where is None or where(r.msg)):
            return i
    return None


def reply_to(recs: Sequence[Rec], msg_id: str, start: int = 0) -> Rec | None:
    return next((r for r in recs[start:] if r.dir == "rx" and r.msg is not None and r.msg.get("re") == msg_id), None)


def exchanges(recs: Sequence[Rec], start: int = 0, end: int | None = None) -> tuple[Exchange, ...]:
    window = recs[start:end]
    replies: dict[str, Rec] = {}
    for r in window:
        if r.dir == "rx" and r.msg is not None and isinstance(r.msg.get("re"), str):
            replies.setdefault(r.msg["re"], r)
    requests = (r for r in window if r.dir == "tx" and r.msg is not None and "re" not in r.msg and "id" in r.msg)
    out = []
    for r in requests:
        reply = replies.get(r.msg["id"])
        out.append(Exchange(r.msg["id"], r.msg.get("t", ""), r.t, reply.t if reply else None,
                            reply.msg if reply else None))  # fmt: skip
    return tuple(out)


def latencies(exs: Sequence[Exchange]) -> list[float]:
    return [round(e.reply_at - e.sent_at, 6) for e in exs if e.reply_at is not None]


def stuck(exs: Sequence[Exchange], now: float, limit_s: float) -> tuple[Exchange, ...]:
    """Requests answered later than limit_s, or still unanswered limit_s after they were sent."""
    return tuple(e for e in exs if ((e.reply_at if e.reply_at is not None else now) - e.sent_at) > limit_s)


def not_ready(recs: Sequence[Rec], start: int = 0) -> int:
    """Device replies E_NOT_READY: the device had left Ready (05 §5.8) while the host still was."""
    return sum(1 for r in recs[start:] if r.dir == "rx" and r.msg and r.msg.get("code") == "E_NOT_READY")


def nudges(recs: Sequence[Rec], start: int = 0, end: int | None = None) -> int:
    return sum(1 for r in recs[start:end] if r.dir == "tx" and r.raw.replace(b"\r", b"") == b"\n")


@dataclass(frozen=True, slots=True)
class Handshake:
    """One port open: when it opened, when the device hello arrived, how many host hellos it took."""

    opened_at: float
    ready_at: float | None
    attempts: int
    hello: Msg | None

    @property
    def seconds(self) -> float | None:
        return None if self.ready_at is None else self.ready_at - self.opened_at


def _is_device_hello(r: Rec) -> bool:
    return r.dir == "rx" and r.msg is not None and r.msg.get("t") == "hello" and "re" in r.msg


def handshakes(recs: Sequence[Rec]) -> tuple[Handshake, ...]:
    """One entry per "open" record, up to the next open (the tap records opens and closes)."""
    opens = [i for i, r in enumerate(recs) if r.dir == "open"] + [len(recs)]
    out = []
    for start, end in zip(opens, opens[1:], strict=False):
        window = recs[start + 1 : end]
        reply = next((r for r in window if _is_device_hello(r)), None)
        stop = window.index(reply) if reply else len(window)
        attempts = sum(1 for r in window[:stop] if r.dir == "tx" and r.msg is not None and r.msg.get("t") == "hello")
        out.append(Handshake(recs[start].t, reply.t if reply else None, attempts, reply.msg if reply else None))
    return tuple(out)


@dataclass(frozen=True, slots=True)
class Dropper:
    """Drops the next `remaining` host lines of type t before they reach the port (fault injection)."""

    t: str
    remaining: int

    def decide(self, msg: Msg | None) -> tuple[Dropper, bool]:
        if msg is not None and msg.get("t") == self.t and self.remaining > 0:
            return replace(self, remaining=self.remaining - 1), True
        return self, False


@dataclass(frozen=True, slots=True)
class RxHold:
    """Withholds device lines of type t from the daemon until the next device hello (fault injection).

    With t = "outbox.item" the device stays Ready (it still takes debug.key) but the host never hears
    its items, so they stay pending on the device's store as if it were offline. The next handshake
    (a device hello) ends the hold; it also puts the device's in-flight items back to pending.
    """

    t: str

    def decide(self, msg: Msg | None) -> tuple[RxHold | None, bool]:
        if msg is not None and msg.get("t") == "hello" and "re" in msg:
            return None, False
        return self, msg is not None and msg.get("t") == self.t
