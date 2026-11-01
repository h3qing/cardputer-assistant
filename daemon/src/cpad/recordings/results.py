"""ResultWatcher (docs/11 §6.6), run from the housekeeping tick. I/O shell around contract.py.

- watches archived sessions without a done result (failed ones stay watched, so a manual rerun, i.e.
  deleting result.json, is picked up); it stats status.json and result.json and reads them only when
  their mtime changed;
- result.json is untrusted: a regular file (no symlink) of at most 64 KiB, valid contract v1 (unknown
  fields ignored; v != 1 ignored and logged), session equal to the archived session;
- rec.result is appended once per (session, state); status.json only updates in-memory state;
- rec.stale once transcribe_wait_h hours after archiving when no result.json was ever accepted.
v0.4.1: unchecked_todos() counts the open boxes of summary.md for the recordings app (docs/13 §8.2).
docs/23 §2.4: rec.result / rec.stale carry the session's effective project (filing.fold.projects), so a
session filed before its result gets its line with the new project.
Logs carry codes and field paths only, never titles or text.
"""

from __future__ import annotations

import json
import logging
import os
import re
import stat
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

from cpad.events.records import iso
from cpad.recordings import contract, index, records

log = logging.getLogger("cpad.rec")
NOT_FOUND = (FileNotFoundError, NotADirectoryError)
Status = tuple[str, str | None]
SUMMARY_MAX = 64 * 1024
UNCHECKED = re.compile(rb"^[ \t]*[-*+] \[ \]", re.MULTILINE)


@dataclass(frozen=True, slots=True)
class Unreadable:
    """Why a file could not be used; never its content (it may hold transcript text)."""

    reason: str


def read_small(path: Path, max_bytes: int) -> tuple[float, Any] | None:
    """(mtime, parsed JSON or Unreadable) of a regular file; None when it does not exist."""
    try:
        st = os.lstat(path)
    except NOT_FOUND:
        return None
    if not stat.S_ISREG(st.st_mode):
        return st.st_mtime, Unreadable("(type)")
    if st.st_size > max_bytes:
        return st.st_mtime, Unreadable("(size)")
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, "rb") as fh:
            return st.st_mtime, json.loads(fh.read(max_bytes + 1))
    except NOT_FOUND:
        return None
    except (OSError, ValueError, RecursionError):
        return st.st_mtime, Unreadable("(root)")


def unchecked_todos(path: Path) -> int | None:
    """Open '- [ ]' boxes in a summary.md WhisperWoof wrote; None when it is missing, not a regular file or
    larger than SUMMARY_MAX (the caller then keeps the result's action count). Never logs its content."""
    try:
        st = os.lstat(path)
        if not stat.S_ISREG(st.st_mode) or st.st_size > SUMMARY_MAX:
            return None
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, "rb") as fh:
            return len(UNCHECKED.findall(fh.read(SUMMARY_MAX)))
    except OSError:
        return None


def _ms(iso_text: Any) -> int | None:
    try:
        return int(datetime.fromisoformat(iso_text).timestamp() * 1000)
    except (TypeError, ValueError):
        return None


def result_record(doc: Mapping[str, Any], session: Mapping[str, Any], at: str) -> dict[str, Any]:
    memo = doc.get("memo") or {}
    error = doc.get("error") or {}
    return records.result(rec=doc["session"], state=doc["state"], level=doc.get("level"), title=doc.get("title"),
                          memo_text=memo.get("text"), memo_intent=memo.get("intent"),
                          actions_n=len(doc.get("actions") or ()), error_code=error.get("code"), session=session,
                          at=at)  # fmt: skip


class ResultWatcher:
    def __init__(self, notes_dir: Path, wait_h: float) -> None:
        self.notes_dir = notes_dir
        self.wait_h = wait_h
        self._seen: Mapping[Path, float] = {}
        self._status: Mapping[str, Status] = {}

    def statuses(self) -> Mapping[str, Status]:
        return dict(self._status)

    def scan(self, idx: index.RecIndex, now_ms: int,
             projects: Mapping[str, str | None] | None = None) -> tuple[dict[str, Any], ...]:  # fmt: skip
        """New rec.result / rec.stale records; `projects` (session -> effective project) overrides the recorded one."""
        out: list[dict[str, Any]] = []
        for rec, archived in sorted(idx.archived.items()):
            if "done" in index.result_states(idx, rec):
                continue
            session = {**archived, "project": projects[rec]} if projects and rec in projects else archived
            folder = self.notes_dir / session["dir"]
            self.read_status(rec, folder / "status.json")
            found = self._read_result(rec, folder / "result.json", session, idx, now_ms)
            if found is None and self._is_stale(rec, session, idx, now_ms):
                found = records.stale(rec=rec, session=session, at=iso(now_ms / 1000))
            out += [found] if found is not None else []
        return tuple(out)

    def _fresh(self, path: Path) -> tuple[bool, Any]:
        """(exists, content when it changed since the last scan else None)."""
        got = read_small(path, contract.RESULT_MAX)
        if got is None:
            self._seen = {k: v for k, v in self._seen.items() if k != path}
            return False, None
        if self._seen.get(path) == got[0]:
            return True, None
        self._seen = {**self._seen, path: got[0]}
        return True, got[1]

    def read_status(self, rec: str, path: Path) -> None:
        exists, doc = self._fresh(path)
        if exists and doc is None:
            return
        valid = exists and contract.check_status(doc).ok and doc["session"] == rec
        kept = {k: v for k, v in self._status.items() if k != rec}
        self._status = {**kept, rec: (doc["state"], doc.get("reason"))} if valid else kept

    def _read_result(self, rec: str, path: Path, session: Mapping[str, Any], idx: index.RecIndex,
                     now_ms: int) -> dict[str, Any] | None:  # fmt: skip
        _, doc = self._fresh(path)
        if doc is None:
            return None
        unreadable = isinstance(doc, Unreadable)
        verdict = contract.Verdict("E_INVALID", (doc.reason,)) if unreadable else contract.check_result(doc)
        if not verdict.ok or doc["session"] != rec:
            fields = list(verdict.fields) if not verdict.ok else ["session"]
            log.warning("result.json ignored", extra={"fields": {"rec": rec, "code": verdict.code, "field": fields}})
            return None
        if doc["state"] in index.result_states(idx, rec):
            return None
        return result_record(doc, session, iso(now_ms / 1000))

    def _is_stale(self, rec: str, session: Mapping[str, Any], idx: index.RecIndex, now_ms: int) -> bool:
        archived_ms = _ms(session.get("at"))
        if rec in idx.stale or idx.results.get(rec) or archived_ms is None:
            return False
        return now_ms - archived_ms >= self.wait_h * 3_600_000


def offline_statuses(notes_dir: Path, idx: index.RecIndex) -> Mapping[str, Status]:
    """status.json of every archived, not yet done session, read directly (cpa without a running daemon)."""
    watcher = ResultWatcher(notes_dir, wait_h=float("inf"))
    for rec, session in idx.archived.items():
        if "done" not in index.result_states(idx, rec):
            watcher.read_status(rec, notes_dir / session["dir"] / "status.json")
    return watcher.statuses()
