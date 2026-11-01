"""Shell: a Recordings service on a temporary $CPA_HOME / notes_dir, connected to FakeFiles via SimLink."""

from __future__ import annotations

import asyncio
import contextlib
import json
import time
from datetime import UTC
from pathlib import Path

from cpad.config.settings import Config
from cpad.events.log import EventLog
from cpad.notes.writer import NotesWriter
from cpad.projects.registry import Project
from cpad.protocol import messages as m
from cpad.protocol.file_messages import Files
from cpad.recordings.service import Recordings
from tests.support.fake_files import FakeFiles
from tests.support.rec_sim import SimLink

DEV = "020000000001"
BOOT = "K3M9Q2T-00042"


async def until(predicate, timeout: float = 5.0, interval: float = 0.01):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        await asyncio.sleep(interval)
    raise AssertionError(f"condition not met within {timeout}s")


class Shell:
    def __init__(self, tmp: Path, files: FakeFiles, *, config: Config | None = None) -> None:
        self.tmp, self.files = tmp, files
        self.home, self.notes_dir = tmp / "home", tmp / "notes"
        self.home.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.config = config or Config(notes_dir=str(self.notes_dir))
        self.hello_up_ms = 900000  # the device's up_ms on the next hello (a re-hello comes later in the boot)
        self.sim: SimLink | None = None
        self.service = self._service()

    def _service(self) -> Recordings:
        log = EventLog(self.home / "events.jsonl")
        history = log.load()
        service = Recordings(events=log, notes=NotesWriter(self.notes_dir, UTC), staging=self.home / "rec-staging",
                             config=lambda: self.config, projects=lambda: (Project("cpa", "Cardputer", 1, "/r"),),
                             tz=UTC, retry_s=0.1)  # fmt: skip
        service.absorb(history)
        return service

    def restart(self) -> None:
        self.service = self._service()

    def hello(self) -> m.DeviceHello:
        return m.DeviceHello(id="d1", re="h1", fw="cpa-fw/0.3.0", dev=DEV, boot=BOOT, up_ms=self.hello_up_ms,
                             rst="poweron", time=False, pending=0, bat_mv=4100, revs=m.Revs(), store="sd",
                             files=Files(**self.files.status()))  # fmt: skip

    @contextlib.asynccontextmanager
    async def connected(self, drop=None, on_send=None):
        self.sim = SimLink(self.files, drop=drop, on_send=on_send)
        sim = self.sim
        sim.listeners.append(lambda pong: self.service.on_pong(sim, pong))
        sim.rx_seq += 1
        await self.service.on_ready(sim, self.hello())
        try:
            yield self
        finally:
            sim.close()
            await self.service.link_down()

    async def settle(self, seconds: float = 0.15) -> None:
        await asyncio.sleep(seconds)

    def events(self, ev: str) -> list[dict]:
        path = self.home / "events.jsonl"
        lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
        return [e for e in map(json.loads, lines) if e.get("ev") == ev]

    def archived(self) -> list[dict]:
        return self.events("rec.archived")

    def session(self, sid: str) -> Path:
        found = next(e for e in self.archived() if e["rec"] == sid)
        return self.notes_dir / found["dir"]

    def part(self, fid: str) -> Path:
        return self.home / "rec-staging" / DEV / f"{fid}.part"

    def staged(self, fid: str) -> int:
        path = self.part(fid)
        return path.stat().st_size if path.exists() else 0

    def stage(self, fid: str, data: bytes) -> None:
        self.part(fid).parent.mkdir(parents=True, exist_ok=True)
        self.part(fid).write_bytes(data)
