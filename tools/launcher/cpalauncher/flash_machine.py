"""`flash firmware <name> <size>` as an immutable state machine (pure; the caller passes the time in).

AWAIT_READY --READY size--> STREAMING --ACK == sent, more to send--> STREAMING (next chunk)
                                      --ACK == size--> AWAIT_RESULT --OK--> DONE(ok)
Only one chunk is ever in flight: the next chunk goes out when the device has acknowledged every byte
sent so far (the device may acknowledge one write in several smaller reads). Any ERR ends it with the
device's own text. A deadline passing, or the port disappearing, before every byte was acknowledged
is a failure; after every byte was acknowledged it is UNCONFIRMED: the Launcher reboots right after
printing OK, so the OK line can be lost and the install must be confirmed another way (verify.py).
"""

from __future__ import annotations

import enum
from dataclasses import dataclass, replace

from cpalauncher.console import Kind, Reply, classify

CHUNK = 2048  # the Launcher's kChunkSize
_NOISE = (Kind.OTHER, Kind.BANNER, Kind.VERSION)  # log lines, boot banners: ignored, no deadline change


class Phase(enum.Enum):
    AWAIT_READY = "await_ready"
    STREAMING = "streaming"
    AWAIT_RESULT = "await_result"
    DONE = "done"


class Status(enum.Enum):
    OK = "ok"  # the Launcher printed OK: verified, partition table written, set to boot
    UNCONFIRMED = "unconfirmed"  # every byte acknowledged, then no OK (lost in the reboot)
    ERROR = "error"  # the Launcher printed ERR, or replied something impossible
    TIMEOUT = "timeout"  # the device stopped answering before every byte was acknowledged
    LOST = "lost"  # the port went away before every byte was acknowledged


@dataclass(frozen=True, slots=True)
class Outcome:
    status: Status
    detail: str


@dataclass(frozen=True, slots=True)
class Timeouts:
    ready_s: float = 30.0
    ack_s: float = 15.0  # the device itself gives up after 5 s without bytes
    result_s: float = 60.0


@dataclass(frozen=True, slots=True)
class SendChunk:
    offset: int
    length: int


@dataclass(frozen=True, slots=True)
class FlashState:
    size: int
    chunk: int
    phase: Phase
    deadline: float
    sent: int = 0
    acked: int = 0
    outcome: Outcome | None = None


def start(size: int, now: float, timeouts: Timeouts, chunk: int = CHUNK) -> FlashState:
    if size <= 0 or chunk <= 0:
        raise ValueError("size and chunk must be positive")
    return FlashState(size, chunk, Phase.AWAIT_READY, now + timeouts.ready_s)


def _done(state: FlashState, status: Status, detail: str) -> FlashState:
    return replace(state, phase=Phase.DONE, outcome=Outcome(status, detail))


def _where(state: FlashState) -> str:
    return f"{state.acked}/{state.size} bytes acknowledged"


def on_line(state: FlashState, line: str, now: float, timeouts: Timeouts) -> tuple[FlashState, SendChunk | None]:
    if state.phase is Phase.DONE:
        return state, None
    reply = classify(line)
    if reply.kind is Kind.ERR:
        return _done(state, Status.ERROR, reply.text), None
    if state.phase is Phase.AWAIT_READY:
        return _on_ready(state, reply, now, timeouts)
    if state.phase is Phase.STREAMING:
        return _on_streaming(state, reply, now, timeouts)
    if reply.kind is Kind.OK:
        return _done(state, Status.OK, reply.text), None
    return state, None


def _on_ready(state: FlashState, reply: Reply, now: float, timeouts: Timeouts) -> tuple[FlashState, SendChunk | None]:
    if reply.kind in _NOISE:
        return state, None
    if reply.kind is not Kind.READY or reply.n != state.size:
        return _done(state, Status.ERROR, f"expected READY {state.size}, got {reply.text!r}"), None
    return _send_next(replace(state, phase=Phase.STREAMING), now, timeouts)


def _on_streaming(
    state: FlashState, reply: Reply, now: float, timeouts: Timeouts
) -> tuple[FlashState, SendChunk | None]:
    if reply.kind in _NOISE:
        return state, None
    valid = reply.kind is Kind.ACK and reply.total == state.size and state.acked <= reply.n <= state.sent
    if not valid:
        return _done(state, Status.ERROR, f"unexpected {reply.text!r} at {_where(state)}"), None
    progressed = replace(state, acked=reply.n, deadline=now + timeouts.ack_s)
    if progressed.acked < progressed.sent:
        return progressed, None
    if progressed.acked == progressed.size:
        return replace(progressed, phase=Phase.AWAIT_RESULT, deadline=now + timeouts.result_s), None
    return _send_next(progressed, now, timeouts)


def _send_next(state: FlashState, now: float, timeouts: Timeouts) -> tuple[FlashState, SendChunk]:
    length = min(state.chunk, state.size - state.sent)
    action = SendChunk(state.sent, length)
    return replace(state, sent=state.sent + length, deadline=now + timeouts.ack_s), action


def on_tick(state: FlashState, now: float) -> FlashState:
    if state.phase is Phase.DONE or now < state.deadline:
        return state
    if state.phase is Phase.AWAIT_RESULT:
        return _done(state, Status.UNCONFIRMED, "every byte was acknowledged but no OK arrived")
    waiting_for = "READY" if state.phase is Phase.AWAIT_READY else "ACK"
    return _done(state, Status.TIMEOUT, f"no {waiting_for} in time ({_where(state)})")


def on_lost(state: FlashState) -> FlashState:
    if state.phase is Phase.DONE:
        return state
    if state.phase is Phase.AWAIT_RESULT:
        return _done(state, Status.UNCONFIRMED, "every byte was acknowledged; the port closed (device reboot)")
    return _done(state, Status.LOST, f"the port closed ({_where(state)})")
