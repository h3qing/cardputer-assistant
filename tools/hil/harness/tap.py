"""Serial tap (I/O shell): the in-process daemon's serial port, recording every line both ways.

The daemon builds its port with serial_factory (05 §7.2 SafeOpen still runs unchanged on it), so
the harness sees the device's replies to debug.* (which the daemon ignores) and every round trip
of a run. It can also drop chosen host lines before they reach the port (the "lost ack" fault of
05 §9.3 item 4) and hold device lines back from the daemon (hold_rx: outbox items stay pending on
the device, 09 §9.3) without any test switch inside cpad. The daemon reads whole lines from the tap
(its own splitter would wait for the newline anyway). The reader thread and the event loop both
write here, so the record is a lock-guarded list; queries go through the pure harness.wire.
"""

from __future__ import annotations

import threading
import time
from collections.abc import Callable
from typing import Any

import serial
from cpad.device.framing import Splitter, split

from harness.wire import Dropper, Rec, RxHold, decode, make_rec


class Tap:
    def __init__(self, clock: Callable[[], float] = time.monotonic):
        self._clock = clock
        self._lock = threading.Lock()
        self._recs: list[Rec] = []
        self._split = {"rx": Splitter(), "tx": Splitter()}
        self._dropper: Dropper | None = None
        self._rx_hold: RxHold | None = None
        self.dropped: list[Rec] = []  # host lines never written and device lines never forwarded

    def factory(self) -> Callable[[], TapSerial]:
        return lambda: TapSerial(self)

    def size(self) -> int:
        with self._lock:
            return len(self._recs)

    def since(self, start: int = 0) -> tuple[Rec, ...]:
        with self._lock:
            return tuple(self._recs[start:])

    def drop_next(self, t: str, n: int = 1) -> None:
        with self._lock:
            self._dropper = Dropper(t, n)

    def hold_rx(self, t: str | None) -> None:
        """Keep device lines of type t from the daemon until the next device hello; None disarms."""
        with self._lock:
            self._rx_hold = RxHold(t) if t else None

    def holding_rx(self) -> bool:
        with self._lock:
            return self._rx_hold is not None

    def _event(self, kind: str, port: str) -> None:
        with self._lock:
            self._split = {"rx": Splitter(), "tx": Splitter()}
            self._recs.append(make_rec(self._clock(), kind, port.encode()))

    def on_open(self, port: str) -> None:
        self._event("open", port)

    def on_close(self, port: str) -> None:
        self._event("close", port)

    def on_rx(self, data: bytes) -> bytes:
        """Record device bytes; returns the whole lines the daemon may read (held ones go to dropped)."""
        now = self._clock()
        with self._lock:
            self._split["rx"], lines = split(self._split["rx"], data)
            kept = []
            for line in lines:
                rec, drop = make_rec(now, "rx", line), False
                if self._rx_hold is not None:
                    self._rx_hold, drop = self._rx_hold.decide(rec.msg)
                (self.dropped if drop else self._recs).append(rec)
                if not drop:
                    kept.append(line)
            return b"".join(kept)

    def on_tx(self, data: bytes) -> bool:
        """Record a host write; False when the dropper swallows it (the port never sees it)."""
        now = self._clock()
        with self._lock:
            if self._dropper is not None:
                self._dropper, drop = self._dropper.decide(decode(data))
                if drop:
                    self.dropped.append(make_rec(now, "tx", data))
                    return False
            self._split["tx"], lines = split(self._split["tx"], data)
            self._recs.extend(make_rec(now, "tx", line) for line in lines)
        return True


class TapSerial(serial.Serial):
    """pyserial's own port with the tap on read / write; control lines are left to SafeOpen."""

    def __init__(self, tap: Tap):
        self._tap = tap
        super().__init__()

    def open(self) -> None:
        super().open()
        self._tap.on_open(str(self.port))

    def read(self, size: int = 1) -> bytes:
        data = super().read(size)
        return self._tap.on_rx(bytes(data)) if data else data

    def write(self, data: Any) -> int | None:
        raw = bytes(data)
        if not self._tap.on_tx(raw):
            return len(raw)
        return super().write(raw)

    def close(self) -> None:
        if self.is_open:
            self._tap.on_close(str(self.port))
        super().close()
