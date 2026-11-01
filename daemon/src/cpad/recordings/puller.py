"""RecPuller: pulls committed recording segments over one link (docs/11 §6.2). I/O shell of pull.py.

One puller per connection (per device boot), one file at a time:
  list all pages -> for each segment in plan order: pull (or only delete when already stored) ->
  after each file, relist if the device's rev grew beyond what the host expects; after the last file,
  relist once more when own deletes took the last listed segment of an unfinished (cut) session off
  the device, so the sink can finish it with gaps (docs/11 §6.4) on this connection.
The sink (recordings.service.Recordings) verifies, lands and records each segment; the puller sends
file.del only after the sink returned True, i.e. after rec.seg.stored is on disk.
A closed link ends the puller; the .part stays in rec-staging for the next connection.
"""

from __future__ import annotations

import asyncio
import logging
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any, Protocol

from cpad.device.link import LinkClosed, Reply, ReplyTimeout
from cpad.protocol.file_messages import FileDel, FileGet, FileItem, FileList, Files, parse_file_data, parse_file_items
from cpad.recordings import pull
from cpad.recordings.land import PartWriter, Staging

log = logging.getLogger("cpad.rec")
RETRYABLE = ("E_BUSY", "E_NOT_READY")


class Sink(Protocol):
    def stored_ids(self, dev: str) -> frozenset[str]: ...

    async def land(self, dev: str, item: FileItem, part: Path) -> bool: ...

    async def deleted(self, dev: str, item: FileItem) -> None: ...

    def unfinished(self, dev: str) -> frozenset[str]: ...

    async def listed(self, dev: str, items: tuple[FileItem, ...]) -> None: ...


class RecPuller:
    def __init__(self, link: Any, dev: str, sink: Sink, staging: Staging, *, retry_s: float = 5.0,
                 clock: Callable[[], float] = time.monotonic):  # fmt: skip
        self._link, self._dev, self._sink, self._staging = link, dev, sink, staging
        self._retry_s, self._clock = retry_s, clock
        self._gate, self._expect = pull.Gate(), pull.Expect()
        self._changed = asyncio.Event()
        self._deleted: frozenset[str] = frozenset()  # own acknowledged deletes since the last listing
        self._finish = False  # one more listing is due to finish a cut session
        self.given_up: frozenset[str] = frozenset()
        self.current: str | None = None
        self.listing: tuple[FileItem, ...] = ()

    @property
    def files(self) -> Files | None:
        return self._gate.files

    def observe(self, files: Files | None, seq: int) -> None:
        """Every hello / pong of this link, with its receive order (link.rx_seq)."""
        self._gate = pull.observe(self._gate, files, seq)
        if files is not None:
            self._expect = pull.observed(self._expect, files.rev, seq)
        self._changed.set()

    async def run(self) -> None:
        try:
            while True:
                await self._until(self._should_list)
                items = await self._list()
                if items is None:
                    await self._pause_after_failure()
                    continue
                await self._work(items)
        except LinkClosed:
            return

    # --- waiting ------------------------------------------------------------------------------

    def _open(self) -> bool:
        return pull.gate_open(self._gate)

    def _should_list(self) -> bool:
        return self._open() and (self._finish or pull.needs_relist(self._expect))

    async def _until(self, predicate: Callable[[], bool]) -> None:
        while not predicate():
            self._changed.clear()
            await self._changed.wait()

    async def _pause_after_failure(self) -> None:
        if self._open():
            await asyncio.sleep(self._retry_s)

    async def _ask(self, msg: Any) -> Reply | None:
        """The reply, or None after a timeout or an error reply (E_BUSY closes the gate)."""
        try:
            reply = await self._link.request_reply(msg)
        except ReplyTimeout:
            return None
        if reply.msg["t"] != "error":
            return reply
        self._note_error(reply)
        return None

    def _note_error(self, reply: Reply) -> None:
        code = reply.msg.get("code")
        if code == "E_BUSY":
            self._gate = pull.on_busy_gate(self._gate, reply.seq)
        log.info("file request refused", extra={"fields": {"code": code, "msg": reply.msg.get("msg")}})

    # --- listing ------------------------------------------------------------------------------

    async def _list(self) -> tuple[FileItem, ...] | None:
        files, seq = self._gate.files, self._gate.seq
        rev, items, after = files.rev, (), None
        while files.n:
            reply = await self._ask(FileList(after=after, max=pull.INFLIGHT))
            if reply is None:
                return None
            page = parse_file_items(reply.msg)
            items += page.items
            if not page.more or not page.items:
                break
            after = page.items[-1].id
        self._expect, self.listing = pull.listed(self._expect, rev, seq), items
        self._deleted, self._finish = frozenset(), False
        await self._sink.listed(self._dev, items)
        return items

    async def _work(self, items: tuple[FileItem, ...]) -> None:
        for action, item in pull.plan(items, stored=self._sink.stored_ids(self._dev), skip=self.given_up):
            await self._until(self._open)
            await (self._delete(item) if action == "delete" else self._pull(item))
            if pull.needs_relist(self._expect):
                return
        self._finish = pull.relist_to_finish(items, deleted=self._deleted, unfinished=self._sink.unfinished(self._dev))

    def _give_up(self, item: FileItem, why: str) -> None:
        self.given_up = self.given_up | {item.id}
        log.warning("segment left on the device for this connection", extra={"fields": {"why": why}})

    # --- delete -------------------------------------------------------------------------------

    async def _delete(self, item: FileItem) -> bool:
        tries = 0
        while tries < pull.STRIKES:
            await self._until(self._open)
            try:
                reply = await self._link.request_reply(FileDel(fid=item.id, size=item.size, sha256=item.sha256))
            except ReplyTimeout:
                tries += 1
                continue
            if reply.msg["t"] == "ack":
                self._expect = pull.deleted(self._expect, reply.seq)
                self._deleted = self._deleted | {item.id}
                await self._sink.deleted(self._dev, item)
                return True
            self._note_error(reply)
            tries += 0 if reply.msg.get("code") == "E_BUSY" else pull.STRIKES
        self._give_up(item, "delete")
        return False

    # --- pull ---------------------------------------------------------------------------------

    async def _pull(self, item: FileItem) -> None:
        path = self._staging.path(self._dev, item.id)
        t = pull.start(item.id, item.size, pull.resume_offset(self._staging.size(path), item.size))
        first, started = t.done, self._clock()
        self.current = item.id
        try:
            with self._staging.open(path, keep=t.done) as part:
                t = await self._transfer(t, part)
                if pull.complete(t):
                    part.sync()
        except OSError as exc:
            log.error("staging write failed", extra={"fields": {"err": exc.errno}})
            t = pull.give_up(t)
        finally:
            self.current = None
        if not pull.complete(t):
            self._give_up(item, "transfer")
            return
        if not await self._sink.land(self._dev, item, path):
            self._give_up(item, "verify")
            return
        elapsed = max(self._clock() - started, 1e-6)
        log.info("segment landed", extra={"fields": {"kb_s": round((t.size - first) / 1024 / elapsed, 1),
                                                       "bytes": t.size - first}})  # fmt: skip
        await self._delete(item)

    async def _transfer(self, t: pull.Transfer, part: PartWriter) -> pull.Transfer:
        pending: dict[asyncio.Future, int] = {}
        try:
            while not (pull.complete(t) or t.failed):
                t, offsets = pull.issue(t, paused=not self._open())
                for off in offsets:
                    pending[self._link.request_reply(FileGet(fid=t.fid, off=off, len=pull.CHUNK))] = off
                if not pending:  # paused by E_BUSY; when open, nothing left to ask means the plan is stuck
                    t = await self._wait_open(t)
                    continue
                done, _ = await asyncio.wait(pending, return_when=asyncio.FIRST_COMPLETED)
                for future in done:
                    t = self._apply(t, pending.pop(future), future, part)
        finally:
            task = asyncio.current_task()
            if pending and not (task and task.cancelling()):
                await asyncio.wait(pending)  # never more than INFLIGHT file.* unanswered
        return t

    async def _wait_open(self, t: pull.Transfer) -> pull.Transfer:
        if self._open():
            return pull.give_up(t)
        await self._until(self._open)
        return t

    def _apply(self, t: pull.Transfer, off: int, future: asyncio.Future, part: PartWriter) -> pull.Transfer:
        try:
            reply = future.result()
        except ReplyTimeout:
            return pull.on_lost(t, off)
        if reply.msg["t"] == "file.data":
            t, chunk = pull.on_data(t, off, parse_file_data(reply.msg))
            if chunk:
                part.append(chunk)
            return t
        self._note_error(reply)
        return pull.on_busy(t, off) if reply.msg.get("code") in RETRYABLE else pull.give_up(t)
