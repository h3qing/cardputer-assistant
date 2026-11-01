"""events.jsonl shell (05 §7.1, §7.3): append-only, fsync on every append, the only authority.

Writers (the daemon and `cpa queue ...`) hold an exclusive flock while they append, readers a
shared one, so nobody sees a half-written line. A partial last line can only be crash debris: it
is skipped and terminated with \\n before the next append. The (dev, id) dedupe index lives here,
rebuilt from the file at start-up; the pure core only learns "duplicate or not".
"""

from __future__ import annotations

import fcntl
import json
import os
from collections.abc import Callable, Iterable, Mapping
from pathlib import Path
from typing import Any

from cpad.config.files import FILE_MODE
from cpad.events.records import dedupe_keys

Prepare = Callable[[tuple[dict, ...]], Iterable[Mapping[str, Any]]]


def _parse(line: bytes) -> dict | None:
    try:
        doc = json.loads(line)
    except ValueError:
        return None
    return doc if isinstance(doc, dict) else None


def _decode(chunk: bytes) -> tuple[tuple[dict, ...], int]:
    """(parsed events, number of unreadable lines)."""
    parsed = tuple(_parse(line) for line in chunk.splitlines())
    events = tuple(doc for doc in parsed if doc is not None)
    return events, len(parsed) - len(events)


class EventLog:
    def __init__(self, path: Path):
        self.path = path
        self._offset = 0
        self._seen: frozenset[tuple[str, str]] = frozenset()
        self.skipped = 0

    @property
    def items_stored(self) -> int:
        return len(self._seen)

    def is_duplicate(self, dev: str, item_id: str) -> bool:
        return (dev, item_id) in self._seen

    def _open(self) -> int:
        fd = os.open(self.path, os.O_RDWR | os.O_APPEND | os.O_CREAT, FILE_MODE)
        os.fchmod(fd, FILE_MODE)
        return fd

    def _read_new(self, fd: int) -> tuple[dict, ...]:
        size = os.fstat(fd).st_size
        data = os.pread(fd, size - self._offset, self._offset) if size > self._offset else b""
        complete = data[: data.rfind(b"\n") + 1]
        tail = data[len(complete) :]
        events, skipped = _decode(complete)
        self.skipped += skipped + (1 if tail else 0)
        self._offset += len(data)
        self._seen = self._seen | dedupe_keys(events)
        return events

    def _locked_read(self) -> tuple[dict, ...]:
        if not self.path.exists():
            return ()
        fd = os.open(self.path, os.O_RDONLY)
        try:
            fcntl.flock(fd, fcntl.LOCK_SH)
            return self._read_new(fd)
        finally:
            os.close(fd)

    def load(self) -> tuple[dict, ...]:
        self._offset, self._seen, self.skipped = 0, frozenset(), 0
        return self._locked_read()

    def poll(self) -> tuple[dict, ...]:
        """Events appended by other processes since the last read."""
        return self._locked_read()

    def append(self, recs: Iterable[Mapping[str, Any]], prepare: Prepare | None = None) -> tuple[tuple, tuple]:
        """Append records and fsync. Returns (events others appended first, records written)."""
        fd = self._open()
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            others = self._read_new(fd)
            written = tuple(dict(r) for r in (prepare(others) if prepare else recs))
            size = os.fstat(fd).st_size
            needs_newline = size > 0 and os.pread(fd, 1, size - 1) != b"\n"
            payload = b"\n" * needs_newline + b"".join(
                json.dumps(r, ensure_ascii=False, separators=(",", ":")).encode() + b"\n" for r in written
            )
            os.write(fd, payload)
            os.fsync(fd)
            self._offset = size + len(payload)
            self._seen = self._seen | dedupe_keys(written)
            return others, written
        finally:
            os.close(fd)
