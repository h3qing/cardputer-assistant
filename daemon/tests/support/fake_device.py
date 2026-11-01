"""FakeDevice: a pty that behaves like cpa-fw's link layer (05 §5.8, §6), for daemon integration tests.

It models the two HWCDC hazards the daemon must survive:
  - TX parking: the first reply after arm_parking() is held until the host writes again;
  - RX overflow: at most rx_capacity unread bytes are kept, the rest is silently dropped.
stall() stops the device from consuming its RX buffer (a busy loop); resume() continues.
v0.3: with `files` (a FakeFiles) the device advertises files on hello / pong and serves file.*.
"""

from __future__ import annotations

import fcntl
import json
import os
import select
import threading
import time
import tty

HELLO_FIELDS = {"rst": "poweron", "time": False, "bat_mv": 4150}
FILE_TYPES = ("file.list", "file.get", "file.del", "debug.rec")


class FakeDevice:
    def __init__(self, *, fw="cpa-fw/0.1.0", dev="020000000001", boot="3QZK1M7-0000C", rx_capacity=8192,
                 foreign=None, park_first_reply=False, lines_per_tick=4, hello_extra=None, files=None):  # fmt: skip
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        flags = fcntl.fcntl(self.master, fcntl.F_GETFL)
        fcntl.fcntl(self.master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        self.path = os.ttyname(self.slave)
        self.fw, self.dev, self.boot = fw, dev, boot
        self.foreign = foreign
        self.hello_extra = dict(hello_extra or {})  # v0.2 hello fields, e.g. {"store": "sd", "launcher": True}
        self.files = files  # v0.3 FakeFiles, or None for a device that cannot serve file.*
        self.rx_capacity, self.lines_per_tick = rx_capacity, lines_per_tick
        self.lock = threading.RLock()
        self.rx = b""
        self.max_rx = 0
        self.dropped = 0
        self.stalled = False
        self.parking = park_first_reply
        self.parked: list[bytes] = []
        self.ready = False
        self.revs = {"projects": 0, "sessions": 0, "queue": 0}
        self.snapshots: dict[str, dict] = {}
        self.received: list[dict] = []
        self.bad_lines: list[bytes] = []
        self.pending: dict[str, dict] = {}
        self.acks: list[dict] = []
        self.results: list[dict] = []
        self.host_writes = 0
        self.seq = 0
        self.started = time.monotonic()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    # --- lifecycle ---------------------------------------------------------------------------

    def start(self) -> FakeDevice:
        self._thread.start()
        return self

    def close(self) -> None:
        self._stop.set()
        self._thread.join(2)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass

    def stall(self) -> None:
        with self.lock:
            self.stalled = True

    def resume(self) -> None:
        with self.lock:
            self.stalled = False

    def arm_parking(self) -> None:
        with self.lock:
            self.parking = True

    def reboot(self, boot: str = "3QZK1M7-0000D") -> None:
        with self.lock:
            self.boot, self.ready, self.started = boot, False, time.monotonic()
            self.revs = {"projects": 0, "sessions": 0, "queue": 0}

    # --- device -> host ----------------------------------------------------------------------

    def up_ms(self) -> int:
        return int((time.monotonic() - self.started) * 1000) + 1000

    def next_id(self) -> str:
        self.seq += 1
        return f"d{self.seq}"

    def write_raw(self, data: bytes) -> None:
        """Write all of `data` (a file.data line is larger than the pty buffer); give up after 2 s."""
        with self.lock:
            view, deadline = memoryview(data), time.monotonic() + 2.0
            while view and time.monotonic() < deadline:
                try:
                    view = view[os.write(self.master, view) :]
                except BlockingIOError:
                    select.select([], [self.master], [], 0.01)
                except OSError:
                    return

    def send(self, msg: dict) -> None:
        """A device-initiated line (outbox.item, agent.*, queue.done, inbox.dismiss)."""
        with self.lock:
            if msg.get("t") == "outbox.item":
                self.pending[msg["id"]] = msg
            self._emit({"v": 0, **msg})

    def _emit(self, msg: dict) -> None:
        line = json.dumps(msg, ensure_ascii=False, separators=(",", ":")).encode() + b"\n"
        if self.parking:
            self.parking = False
            self.parked.append(line)
            return
        self.write_raw(line)

    def _reply(self, re: str, t: str, **fields) -> None:
        self._emit({"v": 0, "t": t, "id": self.next_id(), "re": re, **fields})

    # --- host -> device ----------------------------------------------------------------------

    def _pump(self) -> None:
        ready, _, _ = select.select([self.master], [], [], 0.005)
        if not ready:
            return
        try:
            chunk = os.read(self.master, 65536)
        except (BlockingIOError, OSError):
            return
        with self.lock:
            self.host_writes += 1
            if self.parked:
                self.write_raw(b"".join(self.parked))
                self.parked = []
            room = max(0, self.rx_capacity - len(self.rx))
            self.rx += chunk[:room]
            self.dropped += max(0, len(chunk) - room)
            self.max_rx = max(self.max_rx, len(self.rx))

    def _run(self) -> None:
        while not self._stop.is_set():
            self._pump()
            with self.lock:
                if self.stalled:
                    continue
                for _ in range(self.lines_per_tick):
                    end = self.rx.find(b"\n")
                    if end < 0:
                        break
                    line, self.rx = self.rx[:end], self.rx[end + 1 :]
                    self._line(line.replace(b"\r", b""))

    def _line(self, line: bytes) -> None:
        if not line.startswith(b"{"):
            return
        try:
            msg = json.loads(line)
        except ValueError:
            self.bad_lines.append(line)
            return
        if not isinstance(msg, dict):
            return
        self.received.append(msg)
        if self.foreign:
            self._foreign(msg)
        elif "re" in msg:
            self._host_reply(msg)
        else:
            self._host_request(msg)

    def _foreign(self, msg: dict) -> None:
        if self.foreign == "hello" and msg.get("t") == "hello":
            self._reply(msg["id"], "hello", role="device", fw="other-fw/1.0", dev=self.dev, boot=self.boot,
                        up_ms=self.up_ms(), pending=0, revs=self.revs, **HELLO_FIELDS)  # fmt: skip
        else:
            self.write_raw(b"I (1234) other firmware: unknown command\r\n")

    def _host_reply(self, msg: dict) -> None:
        if msg.get("t") == "outbox.ack":
            self.acks.append(msg)
            if msg.get("status") in ("stored", "duplicate", "rejected"):
                self.pending.pop(msg["re"], None)
        elif msg.get("t") in ("agent.result", "ack", "error"):
            self.results.append(msg)

    def _files_field(self) -> dict:
        return {"files": self.files.status()} if self.files is not None else {}

    def _host_request(self, msg: dict) -> None:
        t, mid = msg.get("t"), msg.get("id")
        if t == "hello":
            self.ready = True
            self._reply(mid, "hello", role="device", fw=self.fw, dev=self.dev, boot=self.boot, up_ms=self.up_ms(),
                        pending=len(self.pending), revs=dict(self.revs), **HELLO_FIELDS,
                        **self.hello_extra, **self._files_field())  # fmt: skip
        elif not self.ready:
            self._reply(mid, "error", code="E_NOT_READY")
        elif t == "ping":
            self._reply(mid, "pong", up_ms=self.up_ms(), pending=len(self.pending), revs=dict(self.revs),
                        **self._files_field())  # fmt: skip
        elif self.files is not None and t in FILE_TYPES:
            reply = self.files.handle(msg)
            self._reply(mid, reply["t"], **{k: v for k, v in reply.items() if k != "t"})
        elif t and t.endswith(".snapshot"):
            kind = t.split(".")[0]
            self.revs[kind] = msg.get("rev", 0)
            self.snapshots[kind] = msg
            self._reply(mid, "ack")
        else:
            self._reply(mid, "ack")

    # --- assertions helpers ------------------------------------------------------------------

    def host_messages(self, t: str) -> list[dict]:
        with self.lock:
            return [m for m in self.received if m.get("t") == t]

    def result_for(self, msg_id: str) -> dict | None:
        with self.lock:
            return next((m for m in self.results + self.acks if m.get("re") == msg_id), None)
