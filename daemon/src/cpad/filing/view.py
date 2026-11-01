"""Filing for the readers outside cpad (cpa rec, cpa filing, MCP, export): events.jsonl and projects.json, read once.
I/O shell.

filing_rows() are the rows of the filing list (filing.rows) for the export's data/filing.jsonl: hidden sessions
too, oldest first; the device ids need $CPA_HOME/id_key, which only cpad creates (the export writes nothing there).
listing() and detail() are `cpa filing ls` and `show` (docs/23 §3.1, §3.2; protocol/filing/v1 ls and show): the
filters and pages of filing.query over light rows (no file read), then the files of the chosen page only; the
text filter reads the title, the memo text and the transcript. Every text goes through display_safe. MCP list_unfiled
and get_filing_item (docs/23 §3.5) are these same two functions; on_device() is the one count only a running cpad
knows, asked with the fixed status request and given ON_DEVICE_S, so a hung cpad costs the list that count, never
WhisperWoof's 5 s. read_statuses() reads status.json confined like every other file (docs/15 §3.8: no symlink at any
level). segment() is the audio `cpa filing triage` plays: a confined descriptor, never a path.
A projects.json that does not load counts as no projects here (the reader still answers; only which keys are
this Mac's and the slot of a meeting's last segment depend on it). cpad keeps its own copy of the same fold
(recordings.service), so both sides compute one answer from one log.
"""

from __future__ import annotations

import json
import posixpath
import re
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from datetime import date
from itertools import islice
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad.archive import fs, ids, recordings
from cpad.archive.fs import ArchiveError, Root
from cpad.config.host import stored_id_key
from cpad.config.settings import Paths
from cpad.control import client
from cpad.events.log import EventLog
from cpad.filing import fold, query, rows
from cpad.filing.args import E_UNKNOWN_ID, VERSION, FilingError, parse_id
from cpad.filing.text import display_safe, excerpt
from cpad.projects.registry import ProjectsError, load_registry
from cpad.protocol.validate import is_int
from cpad.recordings import contract, index

HEAD_SENTENCES = 20  # a transcript's first sentences, cut to rows.TEXT_MAX
MEMO_MAX, QUEUE_MAX, ACTION_MAX, SHORT_MAX, FW_MAX = 2000, 500, 200, 40, 64  # protocol/filing/v1 common.schema.json
SEG_FILE = re.compile(r"seg-[0-9]{3,4}\.wav\Z")
AUDIO_MAX = 32 << 20  # a segment is at most 20 MB (recording contract v1), as archive.export
ON_DEVICE_S = 0.5


def known(path: Path) -> fold.Known:
    try:
        return fold.Known.of(load_registry(path))
    except (ProjectsError, OSError):
        return fold.Known()


@dataclass(frozen=True, slots=True)
class Book:
    """The two folds of one events.jsonl read, and this Mac's projects."""

    idx: index.RecIndex
    log: fold.FilingLog
    known: fold.Known

    @classmethod
    def of(cls, events: Iterable[Mapping[str, Any]], known: fold.Known) -> Book:
        found = tuple(events)
        return cls(index.fold(found), fold.fold(found), known)

    def projects(self) -> Mapping[str, str | None]:
        """Session id (as in events) -> effective project."""
        return fold.projects(self.idx, self.log, self.known)

    def by_sid(self) -> Mapping[str, str | None]:
        """The same, keyed by the lower-case session id the archive readers use (archive.recordings.RecRef.sid)."""
        return MappingProxyType({rec.lower(): project for rec, project in self.projects().items()})

    def envs(self) -> Mapping[str, dict[str, Any] | None]:
        """Lower-case session id -> the Mac-side stamp of its rec.archived (rows.env_of), None without one."""
        return MappingProxyType({rec.lower(): rows.env_of(session) for rec, session in self.idx.archived.items()})


def load(paths: Paths) -> Book:
    return Book.of(EventLog(paths.events).load(), known(paths.projects))


def on_device(paths: Paths) -> int | None:
    """Segments still on the device, from a running cpad; None when it is not running or does not say."""
    try:
        doc = client.request(paths.sock, {"cmd": "status"}, timeout=ON_DEVICE_S)
    except (client.DaemonNotRunning, OSError, ValueError):
        return None
    recs = doc.get("recordings") if isinstance(doc, Mapping) else None
    found = recs.get("on_device") if isinstance(recs, Mapping) else None
    return found if is_int(found) and found >= 0 else None


def _ref(session: Mapping[str, Any]) -> recordings.RecRef | None:
    """The archive directory an archived record names, when it is a well-formed recording directory."""
    parts = str(session.get("dir", "")).split("/")
    split = ids.split_rec_dir(parts[2]) if len(parts) == 3 and parts[0] == "recordings" else None
    return recordings.RecRef(parts[1], parts[2], split[1]) if split and ids.is_date(parts[1]) else None


def _status(root: Root, ref: recordings.RecRef, rec: str) -> rows.Status | None:
    try:
        doc = json.loads(fs.read_bytes(root, (*ref.parts, "status.json"), recordings.STATUS_MAX))
    except (ArchiveError, ValueError, RecursionError):
        return None
    return (doc["state"], doc.get("reason")) if contract.check_status(doc).ok and doc["session"] == rec else None


def read_statuses(root: Root, idx: index.RecIndex) -> Mapping[str, rows.Status]:
    """status.json of every archived session not done yet; a missing, unsafe or malformed one counts as none."""
    pending = ((rec, _ref(s)) for rec, s in idx.archived.items() if "done" not in index.result_states(idx, rec))
    found = ((rec, _status(root, ref, rec)) for rec, ref in pending if ref is not None)
    return MappingProxyType({rec: status for rec, status in found if status is not None})


def _head(root: Root, ref: recordings.RecRef) -> str | None:
    try:
        lines = recordings.sentences(root, ref)
    except ArchiveError:
        return None
    return " ".join(text for _, text in islice(lines, HEAD_SENTENCES))


def _files(root: Root, book: Book, rec: str, tx: str) -> rows.Files:
    ref = _ref(book.idx.archived[rec])
    if ref is None:
        return rows.NO_FILES
    wants = tx in ("transcript", "done") and not rows.done_result(book.idx, rec).get("memo_text")
    return rows.Files(tz_min=recordings.tz_min(root, ref), transcript=_head(root, ref) if wants else None)


def _when(session: Mapping[str, Any]) -> tuple[int, str]:
    started = session.get("started_ms")
    return (started if isinstance(started, int) else 0), str(session.get("rec"))


def _in_range(session: Mapping[str, Any], start: date | None, end: date | None) -> bool:
    if start is None and end is None:
        return True
    ref = _ref(session)
    return ref is not None and ids.in_range(ref.date, start, end)


def _row(root: Root, book: Book, f: fold.Filing, statuses: Mapping[str, rows.Status],
         key: bytes | None) -> dict[str, Any]:  # fmt: skip
    files = _files(root, book, f.rec, rows.tx_of(book.idx, f.rec, statuses))
    return rows.row(f, book.idx, book.known, statuses=statuses, key=key, files=files)


def filing_rows(paths: Paths, root: Root, start: date | None = None,
                end: date | None = None) -> tuple[dict[str, Any], ...]:  # fmt: skip
    """Every archived session whose directory's day is in [start, end], oldest first, hidden ones included."""
    book = load(paths)
    statuses, key = read_statuses(root, book.idx), stored_id_key(paths.id_key)
    found = (f for f in fold.filings(book.idx, book.log, book.known) if _in_range(book.idx.archived[f.rec], start, end))
    chosen = sorted(found, key=lambda f: _when(book.idx.archived[f.rec]))
    return tuple(_row(root, book, f, statuses, key) for f in chosen)


# --- cpa filing ls / show ------------------------------------------------------------------------


def _day(session: Mapping[str, Any]) -> str | None:
    ref = _ref(session)
    return ref.date if ref else None


def _sentences(root: Root, session: Mapping[str, Any]) -> tuple[recordings.Line, ...] | None:
    ref = _ref(session)
    try:
        return recordings.sentences(root, ref) if ref else None
    except ArchiveError:
        return None


def _haystack(root: Root, book: Book, rec: str) -> tuple[str | None, ...]:
    done = rows.done_result(book.idx, rec)
    lines = _sentences(root, book.idx.archived[rec]) or ()
    return done.get("title"), done.get("memo_text"), *(text for _, text in lines)


def listing(paths: Paths, root: Root, raw: Mapping[str, Any], on_device: int | None) -> dict[str, Any]:
    """One page of `cpa filing ls --json`; raw as filing.query.parse takes it (FilingError for a bad one)."""
    book = load(paths)
    q = query.parse(raw, book.known)
    statuses, key = read_statuses(root, book.idx), stored_id_key(paths.id_key)
    found = {f.rec: f for f in fold.filings(book.idx, book.log, book.known)}
    light = tuple(query.Entry(rows.row(f, book.idx, book.known, statuses=statuses, key=key),
                              _day(book.idx.archived[rec])) for rec, f in found.items())  # fmt: skip
    text = q.text
    matches = (lambda e: True) if text is None else (lambda e: query.contains(_haystack(root, book, e.row["id"]), text))
    chosen, cursor = query.page(query.candidates(light, q), q, matches)
    page = [query.shown(_row(root, book, found[e.row["id"]], statuses, key), q) for e in chosen]
    return {"v": VERSION, "counts": query.counts((e.row for e in light), on_device), "next_cursor": cursor,
            "rows": page}  # fmt: skip


def _todo(action: recordings.Action) -> dict[str, Any] | None:
    text = excerpt(action.text, ACTION_MAX)
    return None if text is None else {"n": action.n, "text": text, "owner": excerpt(action.owner, SHORT_MAX),
                                      "due": excerpt(action.due, SHORT_MAX), "t_s": action.t_s,
                                      "confident": action.confident}  # fmt: skip


def _todos(root: Root, session: Mapping[str, Any]) -> list[dict[str, Any]]:
    ref = _ref(session)
    result = recordings.detail(root, ref).result if ref else None
    found = (_todo(a) for a in (result.actions if result else ()))
    return [t for t in found if t is not None]


def _audio(idx: index.RecIndex, rec: str) -> list[str]:
    names = (posixpath.basename(str(s.get("file", ""))) for s in index.segments(idx, rec))
    return [] if rec in idx.swept else [n for n in names if SEG_FILE.match(n)]


def _fw(idx: index.RecIndex, rec: str) -> str | None:
    segs = index.segments(idx, rec)
    fw = segs[0].get("fw") if segs else None
    return excerpt(fw if isinstance(fw, str) else None, FW_MAX)


def _queue_text(events: Iterable[Mapping[str, Any]], hand: fold.Hand | None) -> str | None:
    if hand is None:
        return None
    added = (e.get("text") for e in events if e.get("ev") == "queue.added" and e.get("qid") == hand.qid)
    text = next(added, None)
    return excerpt(text if isinstance(text, str) else None, QUEUE_MAX)


def transcript(root: Root, events: Iterable[Mapping[str, Any]], rec_text: str) -> str | None:
    """hand (docs/23 §3.4): a memo's whole transcript, its sentences joined by a space; None when it has none. A
    meeting's is never read: it is never handed."""
    session = index.fold(events).archived.get(parse_id(rec_text))
    lines = _sentences(root, session) if session is not None and session.get("mode") == "memo" else None
    return " ".join(text for _, text in lines) if lines else None


def segment(paths: Paths, root: Root, rec_text: str) -> tuple[str, int]:
    """The first segment WAV of a session in its archive directory, by name, and a confined read-only descriptor of
    it (fs.open_fd: no symlink at any level, a regular file inside the root, at most AUDIO_MAX), which the caller
    closes. ArchiveError when it has none (not pulled yet, swept) or it is unsafe; FilingError for an unknown id."""
    session = index.fold(EventLog(paths.events).load()).archived.get(parse_id(rec_text))
    ref = _ref(session) if session is not None else None
    if ref is None:
        raise FilingError(E_UNKNOWN_ID, "id")
    names = [n for n in fs.list_names(root, ref.parts, kind="file") if SEG_FILE.match(n)]
    if not names:
        raise ArchiveError("E_NOT_FOUND")
    return names[0], fs.open_fd(root, (*ref.parts, names[0]), AUDIO_MAX)


def detail(paths: Paths, root: Root, rec_text: str) -> dict[str, Any]:
    """`cpa filing show ID --json`: the row, the whole memo text, every sentence, the todos, the audio file names,
    the firmware and the handed queue item's text."""
    rec = parse_id(rec_text)
    events = EventLog(paths.events).load()
    book = Book.of(events, known(paths.projects))
    session = book.idx.archived.get(rec)
    if session is None:
        raise FilingError(E_UNKNOWN_ID, "id")
    f = fold.filing_of(book.idx, book.log, rec, book.known)
    statuses, key = read_statuses(root, book.idx), stored_id_key(paths.id_key)
    memo = rows.done_result(book.idx, rec).get("memo_text")
    lines = _sentences(root, session)
    return {"v": VERSION, "row": _row(root, book, f, statuses, key),
            "text": excerpt(memo if isinstance(memo, str) else None, MEMO_MAX),
            "sentences": None if lines is None else [{"t_s": t, "text": display_safe(x)} for t, x in lines],
            "todos": _todos(root, session), "audio": _audio(book.idx, rec), "fw": _fw(book.idx, rec),
            "queue_text": _queue_text(events, f.hand)}  # fmt: skip
