"""Integration rig: FakeDevice (pty) + fake-claude + an in-process Daemon on a temporary $CPA_HOME."""

from __future__ import annotations

import asyncio
import contextlib
import json
import time
from dataclasses import dataclass, field
from pathlib import Path

import serial

from cpad.app import Daemon
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.device.link import LinkTiming
from cpad.device.port_finder import PortInfo
from tests.support.fake_claude import FakeClaude
from tests.support.fake_device import FakeDevice

SERIAL = "02:00:00:00:00:01"
FAST = LinkTiming(retry_s=2.0, attempts=3, ping_s=1.0, timeout_s=15.0, nudge_s=0.5, backoff_s=30.0, scan_s=0.1)


@dataclass
class LineRecorder:
    """(RTS, DTR) as the chip would see them, plus open/close events with timestamps."""

    rts: bool = False
    dtr: bool = False
    history: list = field(default_factory=list)
    events: list = field(default_factory=list)
    before_write: object = None

    def set(self, rts: bool, dtr: bool) -> None:
        self.rts, self.dtr = rts, dtr
        self.history.append((rts, dtr))

    def event(self, name: str) -> None:
        self.events.append((name, time.monotonic()))

    def count(self, name: str) -> int:
        return sum(1 for e, _ in self.events if e == name)


class PtySerial(serial.Serial):
    """Real pyserial open/read/write on a pty; control-line ioctls (ENOTTY on a pty) are recorded instead."""

    def __init__(self, recorder: LineRecorder):
        self._rec = recorder
        super().__init__()

    def open(self):
        self._rec.event("open")
        self._rec.set(True, True)  # macOS raises both lines when a port is opened
        super().open()

    def _update_rts_state(self):
        self._rec.set(self._rts_state, self._rec.dtr)

    def _update_dtr_state(self):
        self._rec.set(self._rec.rts, self._dtr_state)

    def write(self, data):
        if callable(self._rec.before_write):
            self._rec.before_write(bytes(data))
        return super().write(data)

    def close(self):
        if self.is_open:
            self._rec.event("close")
        super().close()


async def until(predicate, timeout: float = 5.0, interval: float = 0.02):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        await asyncio.sleep(interval)
    raise AssertionError(f"condition not met within {timeout}s")


@dataclass
class Rig:
    paths: Paths
    fake: FakeDevice | None
    claude: FakeClaude
    recorder: LineRecorder
    roots: dict
    daemon: Daemon | None = None
    task: asyncio.Task | None = None
    stop: asyncio.Event | None = None
    ports: list = field(default_factory=list)
    timing: LinkTiming = FAST
    housekeeping_s: float = 3.0

    @property
    def notes_dir(self) -> Path:
        return self.claude.dir.parent / "notes"

    def port_lister(self):
        return tuple(self.ports)

    async def start(self) -> Rig:
        self.stop = asyncio.Event()
        factory = lambda: PtySerial(self.recorder)  # noqa: E731
        self.daemon = Daemon(self.paths, port_lister=self.port_lister, serial_factory=factory, timing=self.timing,
                             housekeeping_s=self.housekeeping_s)  # fmt: skip
        self.task = asyncio.create_task(self.daemon.run(self.stop))
        return self

    async def restart(self) -> Rig:
        await self.halt()
        return await self.start()

    async def halt(self) -> None:
        if self.task is not None:
            self.stop.set()
            await asyncio.wait_for(self.task, 10)
            self.task = None

    async def ready(self, timeout: float = 5.0) -> None:
        await until(lambda: self.daemon.link is not None and self.daemon.link.ready, timeout)

    async def synced(self, timeout: float = 5.0) -> None:
        await self.ready(timeout)
        await until(lambda: {"projects", "sessions", "queue"} <= set(self.fake.snapshots), timeout)

    async def request(self, msg: dict, timeout: float = 10.0) -> dict:
        """Send a device request and wait for the host's reply to it."""
        self.fake.send(msg)
        return await until(lambda: self.fake.result_for(msg["id"]), timeout)

    def events(self) -> list[dict]:
        path = self.paths.events
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    async def close(self) -> None:
        with contextlib.suppress(Exception):
            await self.halt()
        if self.fake is not None:
            self.fake.close()


def make_rig(tmp: Path, home: Path, *, projects=(("cpa", 1, None),), config=None, device=None, raw_projects=None,
             timing=FAST, with_device=True, housekeeping_s=3.0) -> Rig:  # fmt: skip
    paths = Paths.at(home)
    home.mkdir(mode=0o700, parents=True, exist_ok=True)
    claude = FakeClaude(tmp / "claude")
    roots = {key: (tmp / "roots" / key) for key, _, _ in projects}
    for root in roots.values():
        root.mkdir(parents=True, exist_ok=True)
    doc = raw_projects or {
        "version": 1,
        "projects": [
            {"key": k, "name": k, "slot": s, "root": str(roots[k].resolve()), "spawn_permission_mode": mode}
            for k, s, mode in projects
        ],  # fmt: skip
    }
    write_json(paths.projects, doc)
    cfg = {"claude_bin": claude.bin, "notes_dir": str(tmp / "notes"), "poll_s": 0.3, "inbox_idle_s": 60,
           "spawn_env": claude.env, "experimental_bg_reply": False, **(config or {})}  # fmt: skip
    write_json(paths.config, cfg)
    fake = FakeDevice(**(device or {})).start() if with_device else None
    rig = Rig(paths, fake, claude, LineRecorder(), {k: str(v.resolve()) for k, v in roots.items()}, timing=timing,
              housekeeping_s=housekeeping_s)  # fmt: skip
    if fake is not None:
        rig.ports.append(PortInfo(fake.path, SERIAL, 0x303A, 0x1001))
    return rig


def note_item(item_id: str, text: str = "idea: dedupe by sid", project: str | None = "cpa", wall_ms=None) -> dict:
    return {"t": "outbox.item", "id": item_id, "kind": "note", "at": {"up_ms": 41000, "wall_ms": wall_ms},
            "body": {"text": text, "project": project}}  # fmt: skip


def queue_item(item_id: str, intent: str, text: str = "write tests", project: str = "cpa", sid=None) -> dict:
    body = {"text": text, "project": project, "intent": intent, **({"sid": sid} if sid else {})}
    return {"t": "outbox.item", "id": item_id, "kind": "queue", "at": {"up_ms": 1}, "body": body}
