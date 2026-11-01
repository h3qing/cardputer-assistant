"""Recordings: cpad's recording pipeline (docs/11 §6), hooked to the link handlers and housekeeping.

- on_ready / on_pong feed the RecPuller of the current link (a new boot or re-handshake gets a new one);
- as the puller's sink it verifies and lands segments, appends rec.seg.stored (fsync) before the
  puller may send file.del, appends rec.seg.deleted on the ack, and archives sessions (manifest.json
  then rec.archived) when complete, or with gaps after an idle full listing;
- housekeep() runs the ResultWatcher (rec.result / rec.stale + daily-note lines) and, hourly, the
  30-day audio retention sweep;
- v0.4.1 (docs/13 §8, §12.2): on_device_request() answers the recordings app: rec.list with one page of
  history (rec.items), rec.act with ack after appending rec.edited / rec.hidden; it touches no file.
- docs/23 §2.4: it keeps the filing fold too; results get the effective project, and housekeeping appends a
  rec.filed (and its '↪ Filed' daily-note line) for a session filed after its line was written. Only cpad writes notes.
- docs/23 §4.2: segment 0's rec.seg.stored carries the env stamp (stamp.py) from this link's hello (link_since),
  its latest pong and config.json's host_label; a stamp that fails is null and never delays the landing.
Logs carry numbers, codes and ids only.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
import os
import posixpath
import time
from collections.abc import Callable, Iterable, Mapping
from dataclasses import replace
from datetime import datetime, tzinfo
from pathlib import Path
from typing import Any

from cpad.config.files import write_json
from cpad.config.settings import Config
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.filing import fold as filing
from cpad.notes.writer import NotesWriter
from cpad.projects.registry import Project
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as rm
from cpad.protocol.file_messages import FileItem
from cpad.protocol.limits import LIMITS
from cpad.recordings import history, index, land, manifest, records, retention, sessions, stamp
from cpad.recordings.puller import RecPuller
from cpad.recordings.results import ResultWatcher, unchecked_todos
from cpad.recordings.wall import Seen, resolve_wall

log = logging.getLogger("cpad.rec")
SWEEP_EVERY_S = 3600.0
ICLOUD = Path("~/Library/Mobile Documents").expanduser()


def notes_warnings(notes_dir: Path, icloud: Path = ICLOUD) -> list[str]:
    """docs/11 §9: a plaintext archive under iCloud Drive would be uploaded."""
    real, cloud = Path(os.path.realpath(notes_dir)), Path(os.path.realpath(icloud))
    inside = real == cloud or cloud in real.parents
    return ["notes_dir is inside iCloud Drive: recordings and transcripts would be uploaded"] if inside else []


class Recordings:
    def __init__(self, *, events: EventLog, notes: NotesWriter, staging: Path, config: Callable[[], Config],
                 projects: Callable[[], tuple[Project, ...]], history: Iterable[Mapping[str, Any]] = (),
                 on_appended: Callable[[tuple[Mapping[str, Any], ...]], None] = lambda _records: None,
                 clock: Callable[[], float] = time.monotonic, wall: Callable[[], float] = time.time,
                 tz: tzinfo | None = None, retry_s: float = 5.0):  # fmt: skip
        self._events, self._notes, self.notes_dir = events, notes, notes.notes_dir
        self._staging = land.Staging(staging)
        self._config, self._projects, self._on_appended = config, projects, on_appended
        self._clock, self._wall, self._tz, self._retry_s = clock, wall, tz, retry_s
        history = tuple(history)
        self.index = index.fold(history)
        self.filing = filing.fold(history)
        self.watcher = ResultWatcher(self.notes_dir, config().transcribe_wait_h)
        self.puller: RecPuller | None = None
        self._task: asyncio.Task | None = None
        self._seen: Seen | None = None
        self._since: Seen | None = None  # the hello of the current link; None while there is none
        self._fw: str | None = None
        self._next_sweep = 0.0

    def absorb(self, events: Iterable[Mapping[str, Any]]) -> None:
        batch = tuple(events)
        self.index = index.fold(batch, self.index)
        self.filing = filing.fold(batch, self.filing)

    def projects(self) -> Mapping[str, str | None]:
        """Session -> effective project (docs/23 §2.1), with this Mac's projects as they are now."""
        return filing.projects(self.index, self.filing, filing.Known.of(self._projects()))

    def _filed(self) -> tuple[dict[str, Any], ...]:
        return filing.pending_filed(self.index, self.filing, filing.Known.of(self._projects()), self._iso())

    # --- link hooks ---------------------------------------------------------------------------

    async def on_ready(self, link: Any, hello: Any) -> None:
        await self.link_down()
        self._seen, self._fw = Seen(hello.boot, hello.up_ms, self._now_ms()), hello.fw
        self._since = self._seen
        if hello.files is None or not self._config().rec_pull:
            return
        self.puller = RecPuller(link, hello.dev, self, self._staging, retry_s=self._retry_s, clock=self._clock)
        self.puller.observe(hello.files, link.rx_seq)
        self._task = asyncio.get_running_loop().create_task(self._run(self.puller))

    def on_pong(self, link: Any, pong: Any) -> None:
        if self._seen is not None:
            self._seen = Seen(self._seen.boot, pong.up_ms, self._now_ms())
        if self.puller is not None:
            self.puller.observe(pong.files, link.rx_seq)

    async def link_down(self) -> None:
        task, self._task, self.puller, self._since = self._task, None, None, None
        if task is not None:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    @staticmethod
    async def _run(puller: RecPuller) -> None:
        try:
            await puller.run()
        except Exception:
            log.exception("recording puller stopped")

    # --- puller sink --------------------------------------------------------------------------

    def stored_ids(self, dev: str) -> frozenset[str]:
        return index.stored_ids(self.index, dev)

    async def land(self, dev: str, item: FileItem, part: Path) -> bool:
        """Verify, land and record one pulled segment; True only once rec.seg.stored is on disk."""
        try:
            verdict = await asyncio.to_thread(land.verify_file, part, item)
        except OSError as exc:
            log.error("staged segment unreadable", extra={"fields": {"err": exc.errno}})
            return False
        if not verdict.ok:
            log.warning("segment failed verification", extra={"fields": {"reason": verdict.reason}})
            with contextlib.suppress(OSError):
                part.unlink()
            return False
        try:
            self._append((await self._store(dev, item, part, verdict.repaired),))  # fsynced before any file.del
        except OSError as exc:
            log.error("landing failed; the segment stays on the device", extra={"fields": {"err": exc.errno}})
            return False
        await self._maybe_archive(item.rec, listed=None)
        return True

    async def _store(self, dev: str, item: FileItem, part: Path, repaired: bool) -> dict[str, Any]:
        folder = self._session_dir(item)
        target = self.notes_dir / folder / land.seg_name(item.seg)
        if repaired:
            await asyncio.to_thread(land.repair_header, part)
        await asyncio.to_thread(land.land, part, target, root=self.notes_dir)
        digest = await asyncio.to_thread(land.file_sha256, target)
        wall = resolve_wall(item.at, item.id, self._seen, self._now_ms())
        return records.seg_stored(dev=dev, item=item, file=f"{folder}/{target.name}", file_sha256=digest, wall=wall,
                                  repaired=repaired, fw=self._fw, at=self._iso(),
                                  env=self._env(item) if item.seg == 0 else None)  # fmt: skip

    def _env(self, item: FileItem) -> dict[str, Any] | None:
        """docs/23 §4.2: from memory only; any failure gives null (the class name is logged, never a value)."""
        try:
            return stamp.env(host=self._config().host_label, seg_id=item.id, at=item.at, since=self._since,
                             seen=self._seen)  # fmt: skip
        except Exception as exc:
            log.warning("env stamp failed; recorded as null", extra={"fields": {"err": type(exc).__name__}})
            return None

    async def deleted(self, dev: str, item: FileItem) -> None:
        try:
            self._append((records.seg_deleted(dev, item.id, self._iso()),))
        except OSError as exc:
            log.error("events.jsonl append failed", extra={"fields": {"err": exc.errno}})

    async def listed(self, dev: str, items: tuple[FileItem, ...]) -> None:
        on_device = frozenset(i.rec for i in items)
        for rec in sorted(self.unfinished(dev)):
            await self._maybe_archive(rec, listed=on_device)

    def unfinished(self, dev: str) -> frozenset[str]:
        """This device's sessions with stored segments and no manifest yet."""
        return frozenset(
            rec for rec in index.open_sessions(self.index) if index.segments(self.index, rec)[0]["dev"] == dev
        )

    def _session_dir(self, item: FileItem) -> str:
        """Fixed at the session's first landing (docs/11 §6.3)."""
        segs = index.segments(self.index, item.rec)
        if segs:
            return posixpath.dirname(segs[0]["file"])
        wall = resolve_wall(item.at, item.id, self._seen, self._now_ms())
        return land.session_dir(item.rec, wall.wall_ms, self._tz)

    async def _maybe_archive(self, rec: str, listed: frozenset[str] | None) -> None:
        segs = index.segments(self.index, rec)
        if rec in self.index.archived or not segs:
            return
        if not (manifest.is_complete(segs) or (listed is not None and manifest.finalisable(segs, listed=listed))):
            return
        projects = self._projects()
        doc = manifest.build(segs, names={p.key: p.name for p in projects}, slots={p.slot: p.key for p in projects},
                             tz_min=self._tz_min(segs[0]["wall_ms"]), written_at=self._local_iso())  # fmt: skip
        folder = posixpath.dirname(segs[0]["file"])
        path = self.notes_dir / folder / "manifest.json"
        try:
            if not path.exists():  # written once (v0.3.0): a crash before rec.archived must not rewrite it
                await asyncio.to_thread(write_json, path, doc)
            self._append((manifest.archived_record(doc, dev=segs[0]["dev"], dir=folder, at=self._iso(),
                                                   dest=manifest.dest_of(segs)),))  # fmt: skip
        except OSError as exc:
            log.error("manifest write failed; retried later", extra={"fields": {"err": exc.errno}})

    # --- housekeeping -------------------------------------------------------------------------

    async def housekeep(self) -> None:
        now_ms = self._now_ms()
        self.watcher.wait_h = self._config().transcribe_wait_h
        self._noted(self.watcher.scan(self.index, now_ms, self.projects()))
        self._noted(self._filed())  # after the results: a line written just now already shows the project
        if self._clock() >= self._next_sweep:
            self._next_sweep = self._clock() + SWEEP_EVERY_S
            swept = retention.sweep(self.notes_dir, self.index, now_ms)
            if swept:
                self._append(swept)

    def _noted(self, found: tuple[Mapping[str, Any], ...]) -> None:
        """Append records that each draw a daily-note line, then write the lines."""
        if found:
            for record in self._append(found):
                self._notes.write(record)

    # --- status -------------------------------------------------------------------------------

    def rows(self) -> tuple[dict[str, Any], ...]:
        puller = self.puller
        return sessions.rows(self.index, statuses=self.watcher.statuses(), listing=puller.listing if puller else (),
                             current=puller.current if puller else None, seen=self._seen,
                             projects=self.projects())  # fmt: skip

    def status(self) -> dict[str, Any]:
        puller = self.puller
        files = puller.files if puller else None
        return sessions.summary(self.rows(), idx=self.index, on_device=files.n if files else None,
                                pulling=bool(puller and puller.current))  # fmt: skip

    # --- recordings app -----------------------------------------------------------------------

    async def on_device_request(self, msg: Any) -> Any:
        """rec.list -> rec.items, rec.act -> ack, or an error reply with re = the request id; never raises."""
        try:
            if isinstance(msg, rm.RecList):
                return self._list(msg)
            if isinstance(msg, rm.RecAct):
                return self._act(msg)
            return m.ErrorMsg(msg.id, "E_UNKNOWN_TYPE")
        except Exception:
            log.exception("recordings request failed", extra={"fields": {"t": msg.T}})
            return m.ErrorMsg(msg.id, "E_INTERNAL")

    def _list(self, msg: rm.RecList) -> Any:
        try:
            items, nxt = history.page(self.rows(), self.index, cursor=msg.cursor, max=msg.max, filter=msg.filter)
        except history.BadCursor:
            return m.ErrorMsg(msg.id, "E_BAD_FIELD", "cursor")
        return rm.RecItems(re=msg.id, items=tuple(self._counted(item) for item in items), next=nxt)

    def _counted(self, item: rm.RecRow) -> rm.RecRow:
        """A summarised meeting's todo_n from the unchecked boxes of summary.md, when that is cheap."""
        session = self.index.archived.get(item.rec)
        if item.mode != "meeting" or item.st != "done" or session is None:
            return item
        found = unchecked_todos(self.notes_dir / session["dir"] / "summary.md")
        return item if found is None else replace(item, todo_n=min(found, history.COUNT_MAX))

    def _act(self, msg: rm.RecAct) -> Any:
        row = next((row for row in self.rows() if row["sid"] == msg.rec), None)
        if row is None:
            return m.ErrorMsg(msg.id, "E_UNKNOWN_ITEM")
        if msg.op == "tag" and msg.project is not None and msg.project not in {p.key for p in self._projects()}:
            return m.ErrorMsg(msg.id, "E_BAD_FIELD", "project")
        found = history.change(row, msg.op, msg.project, self._iso())
        try:
            if found:
                self._append(found)
        except OSError as exc:
            log.error("events.jsonl append failed", extra={"fields": {"err": exc.errno}})
            return m.ErrorMsg(msg.id, "E_INTERNAL")
        log.info("recording edited", extra={"fields": {"rec": msg.rec, "op": msg.op, "changed": bool(found)}})
        return m.Ack(msg.id)

    # --- helpers ------------------------------------------------------------------------------

    def _append(self, recs: tuple[Mapping[str, Any], ...]) -> tuple[Mapping[str, Any], ...]:
        others, written = self._events.append(recs)
        self.absorb((*others, *written))
        self._on_appended((*others, *written))
        return written

    def _now_ms(self) -> int:
        return int(self._wall() * 1000)

    def _iso(self) -> str:
        return iso(self._wall())

    def _local_iso(self) -> str:
        return datetime.fromtimestamp(self._wall(), self._tz).astimezone(self._tz).isoformat(timespec="milliseconds")

    def _tz_min(self, wall_ms: int) -> int:
        when = datetime.fromtimestamp(wall_ms / 1000, self._tz).astimezone(self._tz)
        minutes = int(when.utcoffset().total_seconds() // 60)
        return max(LIMITS["tz_min_min"], min(LIMITS["tz_min_max"], minutes))
