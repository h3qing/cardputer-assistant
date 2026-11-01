"""Daily notes shell (05 §7.8): idempotent appends keyed by the block id, backfill at start-up.

v0.3: rec.result / rec.stale events add recording lines (recordings/route.rec_entry). A block id
counts as present only at the end of a line, so ^cpa-<sid> is not hidden by ^cpa-<sid>-u, and a
block id quoted inside some text does not suppress a line.
"""

from __future__ import annotations

import logging
import os
import re
from collections.abc import Iterable, Mapping
from datetime import tzinfo
from pathlib import Path
from typing import Any

from cpad.notes.render import block_id, note_entry
from cpad.recordings.route import rec_entry

log = logging.getLogger("cpad.notes")
MALFORMED = (ValueError, KeyError, TypeError, AttributeError)


class NotesWriter:
    def __init__(self, notes_dir: Path, tz: tzinfo | None = None):
        self.notes_dir = notes_dir
        self.tz = tz

    def target(self, record: Mapping[str, Any]) -> Path:
        entry = note_entry(record, self.tz)
        if entry is None:
            raise ValueError("not a note record")
        return self.notes_dir / entry[0]

    def _entry(self, record: Mapping[str, Any]) -> tuple[str, str, str] | None:
        note = note_entry(record, self.tz)
        return (*note, block_id(record["id"])) if note is not None else rec_entry(record, self.tz)

    def write(self, record: Mapping[str, Any]) -> bool:
        """True when a line was added; False when it already existed, has no line, is malformed, or failed."""
        try:
            entry = self._entry(record)
        except MALFORMED as exc:
            log.error("note skipped", extra={"fields": {"err": type(exc).__name__}})
            return False
        if entry is None:
            return False
        try:
            return self._append(self.notes_dir / entry[0], entry[1], entry[2])
        except OSError as exc:
            log.error("note write failed; it is backfilled at the next start", extra={"fields": {"err": exc.errno}})
            return False

    def _append(self, path: Path, line: str, marker: str) -> bool:
        self.notes_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        existing = path.read_bytes() if path.exists() else b""
        if re.search(re.escape(marker.encode()) + rb"[ \t\r]*$", existing, re.MULTILINE):
            return False
        prefix = b"\n" if existing and not existing.endswith(b"\n") else b""
        fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
        try:
            os.write(fd, prefix + line.encode("utf-8") + b"\n")
            os.fsync(fd)
        finally:
            os.close(fd)
        return True

    def backfill(self, records: Iterable[Mapping[str, Any]]) -> int:
        """Lines added; a malformed record is skipped and never aborts start-up."""
        return sum(self.write(r) for r in records)
