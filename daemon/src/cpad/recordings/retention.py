"""30-day audio retention (user decision 2026-10-20: audio 30 days, text forever). I/O shell.

A session's seg-NNN.wav files are removed when all of these hold:
  - it was archived at least 30 days ago (archive time is never earlier than the recording);
  - WhisperWoof finished it (a done rec.result: it will not touch the directory again) and both
    transcript.json and summary.md are regular files in the session directory;
  - it was not swept before (rec.audio.swept).
Only cpad's own landed segment files are candidates (paths from rec.seg.stored), and only regular
files named seg-NNN.wav directly inside the session directory; nothing else is ever deleted.
"""

from __future__ import annotations

import logging
import os
import re
import stat
from collections.abc import Mapping
from datetime import datetime
from pathlib import Path
from typing import Any

from cpad.events.records import iso
from cpad.recordings import index, records

log = logging.getLogger("cpad.rec")
KEEP_DAYS = 30
DAY_MS = 86_400_000
SEG_FILE = re.compile(r"seg-[0-9]{3,4}\.wav\Z")
TEXT_NEEDED = ("transcript.json", "summary.md")


def _archived_ms(session: Mapping[str, Any]) -> int | None:
    try:
        return int(datetime.fromisoformat(session["at"]).timestamp() * 1000)
    except (KeyError, TypeError, ValueError):
        return None


def due(idx: index.RecIndex, now_ms: int) -> tuple[str, ...]:
    """Sessions old enough, done, and not swept yet (the file checks come later)."""
    out = []
    for rec, session in sorted(idx.archived.items()):
        archived_ms = _archived_ms(session)
        old = archived_ms is not None and now_ms - archived_ms >= KEEP_DAYS * DAY_MS
        if old and rec not in idx.swept and "done" in index.result_states(idx, rec):
            out.append(rec)
    return tuple(out)


def _regular(path: Path) -> bool:
    try:
        return stat.S_ISREG(os.lstat(path).st_mode)
    except OSError:
        return False


def _candidates(notes_dir: Path, folder: Path, idx: index.RecIndex, rec: str) -> tuple[Path, ...]:
    paths = (notes_dir / seg["file"] for seg in index.segments(idx, rec))
    return tuple(p for p in paths if p.parent == folder and SEG_FILE.match(p.name) and _regular(p))


def _remove(paths: tuple[Path, ...]) -> int:
    removed = 0
    for path in paths:
        try:
            path.unlink()
            removed += 1
        except OSError as exc:
            log.warning("audio sweep could not remove a segment", extra={"fields": {"err": exc.errno}})
    return removed


def sweep(notes_dir: Path, idx: index.RecIndex, now_ms: int) -> tuple[dict[str, Any], ...]:
    """Remove the WAVs of every due session; returns the rec.audio.swept records to append."""
    out = []
    for rec in due(idx, now_ms):
        folder = notes_dir / idx.archived[rec]["dir"]
        if not all(_regular(folder / name) for name in TEXT_NEEDED):
            continue
        removed = _remove(_candidates(notes_dir, folder, idx, rec))
        out.append(records.audio_swept(rec, removed, iso(now_ms / 1000)))
    return tuple(out)
