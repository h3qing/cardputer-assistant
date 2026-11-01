"""DeviceLink (05 §7.2, §6.3, §6.4): one open port, from SafeOpen to release. I/O shell.

- a reader thread splits lines and hands them to the event loop with call_soon_threadsafe;
- host-initiated lines go through a FIFO metered by the credit window (<= 4096 B unanswered),
  snapshots and time.set are coalesced by type; replies to device requests are written at once;
- handshake: hello every retry_s, at most `attempts` times; any attempt's reply counts; a foreign
  firmware or another protocol version releases the port; no valid reply by attempts*retry_s
  (6 s) releases it too;
- Ready: ping every ping_s, 15 s without valid data ends the link, E_NOT_READY re-handshakes,
  a new boot id (or a smaller up_ms) is a reboot and triggers a full resync;
- while a reply is overdue the host writes a blank line every nudge_s: it frees a reply parked in
  the device's HWCDC TX buffer, and the device drops it as noise (ADR-0001 §5.2 caveat);
- request_reply (v0.3 file.*, docs/11 §6.1) queues like request() and returns a future that resolves
  with the device reply matching its id, or fails with ReplyTimeout reply_s after sending / LinkClosed.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
import threading
import time
from collections.abc import Callable, Mapping
from dataclasses import dataclass, replace
from typing import Any, Protocol

import serial

from cpad.device.credit import Credit
from cpad.device.framing import Splitter, split
from cpad.device.handshake import Seen, classify, is_reboot
from cpad.device.port_finder import PortInfo
from cpad.device.safe_open import safe_open
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol.limits import LIMITS

log = logging.getLogger("cpad.link")
EOF = object()
WAKE = object()
COALESCED = ("time.set", "projects.snapshot", "sessions.snapshot", "queue.snapshot", "ping")


@dataclass(frozen=True, slots=True)
class LinkTiming:
    retry_s: float = LIMITS["hello_retry_ms"] / 1000
    attempts: int = LIMITS["hello_attempts"]
    ping_s: float = LIMITS["ping_interval_ms"] / 1000
    timeout_s: float = LIMITS["link_timeout_ms"] / 1000
    nudge_s: float = 0.5
    backoff_s: float = 30.0
    scan_s: float = 1.0
    reclaim_s: float = LIMITS["credit_reclaim_ms"] / 1000
    tick_s: float = 0.05
    reply_s: float = 5.0


@dataclass(frozen=True, slots=True)
class LinkOutcome:
    reason: str
    hello: m.DeviceHello | None = None


@dataclass(frozen=True, slots=True)
class Reply:
    """A device reply to request_reply: the decoded message and its receive order on this link."""

    msg: Mapping[str, Any]
    seq: int


class LinkClosed(ConnectionError):
    """The link ended before the reply arrived."""


class ReplyTimeout(TimeoutError):
    """No reply within reply_s of sending."""


def _consume(future: asyncio.Future) -> None:
    """Mark a failure as retrieved so an abandoned waiter never logs 'exception was never retrieved'."""
    if not future.cancelled():
        future.exception()


class LinkHandlers(Protocol):
    def host_hello(self) -> m.HostHello: ...

    async def on_ready(self, link: DeviceLink, hello: m.DeviceHello, reboot: bool) -> None: ...

    async def on_request(self, link: DeviceLink, decoded: codec.Decoded) -> None: ...

    async def on_pong(self, link: DeviceLink, pong: m.Pong) -> None: ...


class HostIds:
    """h<n> ids, unique for the daemon's lifetime (05 §6.2)."""

    def __init__(self) -> None:
        self._next = 1

    def peek(self) -> str:
        return f"h{self._next}"

    def take(self) -> str:
        current, self._next = self.peek(), self._next + 1
        return current


class DeviceLink:
    def __init__(self, port: PortInfo, handlers: LinkHandlers, *, serial_factory: Callable[[], Any] = serial.Serial,
                 timing: LinkTiming | None = None, ids: HostIds | None = None, seen: Seen | None = None,
                 clock: Callable[[], float] = time.monotonic):  # fmt: skip
        self.port = port
        self.ready = False
        self.hello: m.DeviceHello | None = None
        self.seen = seen
        self.reboots = 0
        self.counters = {"noise": 0, "E_PARSE": 0, "E_TOO_LARGE": 0, "bad_reply": 0}
        self.rx_seq = 0
        self._handlers, self._factory, self._t, self._clock = handlers, serial_factory, timing or LinkTiming(), clock
        self._ids = ids or HostIds()
        self._credit = Credit(reclaim_s=self._t.reclaim_s)
        self._outq: list[tuple[dict, asyncio.Future | None]] = []
        self._waiting: dict[str, tuple[asyncio.Future, float]] = {}
        self._finished = False
        self._closing: str | None = None
        self._serial: Any = None
        self._inbox: asyncio.Queue = asyncio.Queue()
        self._stop = threading.Event()
        self._last_rx = self._last_write = self._next_ping = 0.0

    # --- public API ---------------------------------------------------------------------------

    def request(self, msg: Any) -> None:
        """Queue a host-initiated message (dataclass); sent when the credit window allows."""
        wire = m.to_wire(msg)
        self.request_raw({k: v for k, v in wire.items() if k not in ("v", "id")})

    def request_raw(self, wire: dict) -> None:
        """Queue a host-initiated message given as a wire dict without v / id (e.g. debug.* for HIL)."""
        self._enqueue(wire, None)

    def request_reply(self, msg: Any) -> asyncio.Future:
        """Queue a host request (dataclass) and return a future of its Reply (docs/11 §6.1)."""
        future = asyncio.get_running_loop().create_future()
        future.add_done_callback(_consume)
        if self._finished:  # nobody would ever answer or fail it
            future.set_exception(LinkClosed("closed"))
            return future
        wire = m.to_wire(msg)
        self._enqueue({k: v for k, v in wire.items() if k not in ("v", "id")}, future)
        return future

    def waiting(self) -> int:
        """Sent request_reply calls still waiting for their reply."""
        return len(self._waiting)

    def _enqueue(self, wire: dict, future: asyncio.Future | None) -> None:
        t = wire["t"]
        dropped = [q for q in self._outq if t in COALESCED and q[0]["t"] == t]
        for _, old in dropped:
            if old is not None:
                old.cancel()
        self._outq = [*(q for q in self._outq if q not in dropped), (dict(wire), future)]
        self._pump(self._clock())

    def reply(self, msg: Any) -> None:
        """Answer a device request right away (not metered: device in-flight caps bound these)."""
        wire = m.to_wire(replace(msg, id=self._ids.take()))
        self._write(codec.encode(wire))

    def close(self, reason: str = "closed") -> None:
        self._closing = self._closing or reason
        self._inbox.put_nowait(WAKE)

    def credit_used(self) -> int:
        return self._credit.used(self._clock())

    async def run(self) -> LinkOutcome:
        try:
            self._serial = await asyncio.to_thread(safe_open, self.port.device, self._factory)
        except (serial.SerialException, OSError, ValueError) as exc:
            log.warning("cannot open port", extra={"fields": {"port": self.port.device, "err": type(exc).__name__}})
            return LinkOutcome("open_failed")
        reader = threading.Thread(target=self._reader, args=(asyncio.get_running_loop(),), daemon=True)
        reader.start()
        try:
            return await self._session()
        finally:
            self.ready, self._finished = False, True
            self._fail_all(LinkClosed(self._closing or "closed"))
            await self._shutdown(reader)

    # --- session ------------------------------------------------------------------------------

    async def _session(self) -> LinkOutcome:
        result = await self._handshake()
        if isinstance(result, LinkOutcome):
            return result
        await self._become_ready(result)
        return await self._serve()

    async def _handshake(self) -> m.DeviceHello | LinkOutcome:
        self.ready = False
        attempts: tuple[str, ...] = ()
        start = self._clock()
        deadline, next_try = start + self._t.attempts * self._t.retry_s, start
        while not self._closing:
            now = self._clock()
            if len(attempts) < self._t.attempts and now >= next_try:
                attempts, next_try = (*attempts, self._send_hello(now)), now + self._t.retry_s
            if now >= deadline:
                return LinkOutcome("no_reply")
            line = await self._next_line(min(self._t.tick_s, deadline - now))
            verdict = self._handshake_line(line, attempts) if line is not None else None
            if isinstance(verdict, m.DeviceHello | LinkOutcome):
                return verdict
        return LinkOutcome(self._closing)

    def _handshake_line(self, line: bytes, attempts: tuple[str, ...]) -> Any:
        decoded = codec.decode_line(line)
        self._note_valid(decoded)
        if decoded.ok and "re" in decoded.msg:
            self._credit = self._credit.answered(decoded.msg["re"])
        verdict = classify(decoded, attempts)
        if verdict == "ok":
            return m.from_wire(decoded.msg)
        if verdict in ("foreign", "version"):
            log.warning("port released", extra={"fields": {"port": self.port.device, "reason": verdict}})
            return LinkOutcome(verdict)
        return None

    def _send_hello(self, now: float) -> str:
        msg_id = self._ids.take()
        line = codec.encode(m.to_wire(replace(self._handlers.host_hello(), id=msg_id)))
        self._credit = self._credit.sent(msg_id, len(line), now)
        self._write(line)
        return msg_id

    async def _become_ready(self, hello: m.DeviceHello) -> None:
        reboot = is_reboot(self.seen, hello.boot, hello.up_ms)
        self.seen, self.hello, self.ready = Seen(hello.boot, hello.up_ms), hello, True
        self.reboots += reboot
        now = self._clock()
        self._last_rx, self._next_ping = now, now + self._t.ping_s
        await self._handlers.on_ready(self, hello, reboot)

    async def _serve(self) -> LinkOutcome:
        while not self._closing:
            now = self._clock()
            if now - self._last_rx >= self._t.timeout_s:
                return LinkOutcome("timeout")
            if now >= self._next_ping:
                self._next_ping = now + self._t.ping_s
                self._credit = self._credit.prune(now)
                self.request(m.Ping())
            self._expire(now)
            self._pump(now)
            line = await self._next_line(self._t.tick_s)
            outcome = await self._handle(line) if line is not None else None
            if outcome is not None:
                return outcome
        return LinkOutcome(self._closing)

    async def _handle(self, line: bytes) -> LinkOutcome | None:
        decoded = codec.decode_line(line)
        self._note_valid(decoded)
        msg = decoded.msg
        if decoded.ok and "re" in msg:
            return await self._device_reply(msg)
        if decoded.reply_id is not None:
            await self._handlers.on_request(self, decoded)
        elif decoded.msg is not None and decoded.outcome not in ("ok", "ignored"):
            self.counters["bad_reply"] += 1
        return None

    async def _device_reply(self, msg: dict) -> LinkOutcome | None:
        self._credit = self._credit.answered(msg["re"])
        self._resolve(msg)
        t = msg["t"]
        if t == "error" and msg.get("code") == "E_NOT_READY":
            result = await self._handshake()
            if isinstance(result, LinkOutcome):
                return result
            await self._become_ready(result)
        elif t == "hello" and msg.get("role") == "device":
            hello = m.from_wire(msg)
            if is_reboot(self.seen, hello.boot, hello.up_ms):
                await self._become_ready(hello)
        elif t == "pong":
            await self._pong(m.from_wire(msg))
        return None

    async def _pong(self, pong: m.Pong) -> None:
        if is_reboot(self.seen, None, pong.up_ms) and self.hello is not None:
            await self._become_ready(replace(self.hello, up_ms=pong.up_ms))
            return
        self.seen = Seen(self.seen.boot if self.seen else None, pong.up_ms)
        await self._handlers.on_pong(self, pong)

    # --- I/O ----------------------------------------------------------------------------------

    def _note_valid(self, decoded: codec.Decoded) -> None:
        if decoded.ok:
            self._last_rx = self._clock()
            self.rx_seq += 1
        elif decoded.outcome in self.counters:
            self.counters[decoded.outcome] += 1

    def _pump(self, now: float) -> None:
        if not self.ready:
            return
        while self._outq and not self._closing:
            head, future = self._outq[0]
            wire = {"v": 0, "t": head["t"], "id": self._ids.peek(), **head}
            line = codec.encode(wire)
            if not self._credit.can_send(len(line), now):
                return
            self._outq = self._outq[1:]
            msg_id = self._ids.take()
            self._credit = self._credit.sent(msg_id, len(line), now)
            if future is not None and not future.done():
                self._waiting = {**self._waiting, msg_id: (future, now + self._t.reply_s)}
            self._write(line)

    def _resolve(self, msg: Mapping[str, Any]) -> None:
        entry = self._waiting.get(msg["re"])
        if entry is None:
            return
        self._waiting = {k: v for k, v in self._waiting.items() if k != msg["re"]}
        if not entry[0].done():
            entry[0].set_result(Reply(msg, self.rx_seq))

    def _expire(self, now: float) -> None:
        late = {k: v for k, v in self._waiting.items() if now >= v[1]}
        self._waiting = {k: v for k, v in self._waiting.items() if k not in late}
        for future, _ in late.values():
            if not future.done():
                future.set_exception(ReplyTimeout("no reply"))

    def _fail_all(self, exc: Exception) -> None:
        pending = [f for f, _ in self._waiting.values()] + [f for _, f in self._outq if f is not None]
        self._waiting, self._outq = {}, [(w, None) for w, _ in self._outq]
        for future in pending:
            if not future.done():
                future.set_exception(exc)

    def _write(self, data: bytes) -> None:
        if self._closing or self._serial is None:
            return
        try:
            self._serial.write(data)
            self._last_write = self._clock()
        except (serial.SerialException, OSError) as exc:
            log.warning("write failed", extra={"fields": {"err": type(exc).__name__}})
            self.close("io_error")

    def _maybe_nudge(self, now: float) -> None:
        overdue = self._credit.used(now) > 0 and now - self._last_write >= self._t.nudge_s
        if overdue:
            self._write(b"\n")

    async def _next_line(self, timeout: float) -> bytes | None:
        self._maybe_nudge(self._clock())
        try:
            item = await asyncio.wait_for(self._inbox.get(), max(0.0, timeout))
        except TimeoutError:
            return None
        if item is EOF:
            self.close("io_error")
        return item if isinstance(item, bytes) else None

    def _reader(self, loop: asyncio.AbstractEventLoop) -> None:
        state = Splitter()
        while not self._stop.is_set():
            try:
                chunk = self._serial.read(max(1, self._serial.in_waiting))
            except (serial.SerialException, OSError, TypeError, ValueError):
                chunk = None
            if chunk is None:
                self._post(loop, EOF)
                return
            state, lines = split(state, chunk)
            for line in lines:
                self._post(loop, line)

    def _post(self, loop: asyncio.AbstractEventLoop, item: Any) -> None:
        if not self._stop.is_set():
            with contextlib.suppress(RuntimeError):
                loop.call_soon_threadsafe(self._inbox.put_nowait, item)

    async def _shutdown(self, reader: threading.Thread) -> None:
        self._stop.set()
        with contextlib.suppress(Exception):
            self._serial.cancel_read()
        await asyncio.to_thread(reader.join, 2.0)
        with contextlib.suppress(Exception):
            self._serial.close()
