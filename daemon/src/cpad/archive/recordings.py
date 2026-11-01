"""Recordings in the archive (recording contract v1, docs/15 §3.4). Read-only; every file in them is untrusted.

- a recording is a directory recordings/<YYYY-MM-DD>/<HHMM>-<sid lower> (ids.split_rec_dir), newest first;
- manifest.json (cpad) gives mode, start, duration, project, segments and marks. It is read tolerantly:
  only `v` == 1, a known mode and the directory's session are required;
- result.json and status.json (the transcriber) count only when cpad.recordings.contract accepts them and
  their session is the directory's; otherwise they are treated as absent, never as a failure;
- state, as in `cpa rec ls`: done | transcript | failed | blocked | processing | archived | pulling.
Audio is never read here.
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterator, Mapping
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from itertools import chain, islice
from typing import Any

from cpad.archive import fs, ids
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.paging import BUDGET, clip, take
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import is_int, matches
from cpad.recordings import contract

MANIFEST_MAX = 256 * 1024
STATUS_MAX = 4 * 1024
RESULT_MAX = contract.RESULT_MAX
SUMMARY_MAX = 1 << 20
TRANSCRIPT_MAX = 8 << 20
SCAN_MAX = 10_000
MARKS_MAX = 100
SENTENCE_COST = 32  # JSON around one sentence: {"i":…,"t_s":…,"text":""}
MODES = ("memo", "meeting")
TRANSCRIPTS = ("transcript.json", "transcript.md")
MARK_K = re.compile(r"[a-z][a-z0-9_]{0,15}\Z")
MD_LINE = re.compile(r"- \[(?:([0-9]{1,2}):)?([0-9]{1,3}):([0-9]{2})\] (.*)\Z")  # on a stripped line
MD_BLOCK = re.compile(r" \^t[0-9]{6}(?:-[0-9]+)?\Z")  # no lazy group before a \s*: linear on hostile lines
Doc = Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class RecRef:
    date: str
    dir: str
    sid: str  # lower case, as in the directory name

    @property
    def id(self) -> str:
        return self.sid.upper()

    @property
    def key(self) -> str:
        return f"{self.date}/{self.dir}"

    @property
    def parts(self) -> tuple[str, ...]:
        return ("recordings", self.date, self.dir)


@dataclass(frozen=True, slots=True)
class RecScan:
    refs: tuple[RecRef, ...]
    partial: bool = False
    last: str | None = None  # when partial: the key of the last directory scanned; scan again below it


@dataclass(frozen=True, slots=True)
class Mark:
    t_s: float
    k: str


@dataclass(frozen=True, slots=True)
class Memo:
    text: str
    intent: str


@dataclass(frozen=True, slots=True)
class Action:
    n: int
    text: str
    owner: str | None
    due: str | None
    t_s: float | None
    confident: bool


@dataclass(frozen=True, slots=True)
class RecMeta:
    mode: str
    started: str | None
    duration_s: int | None
    project: str | None
    segments: int
    marks: tuple[Mark, ...]
    consent: str | None


@dataclass(frozen=True, slots=True)
class RecResult:
    state: str
    level: str | None
    title: str | None
    memo: Memo | None
    actions: tuple[Action, ...]


@dataclass(frozen=True, slots=True)
class RecRow:
    id: str
    date: str
    started: str | None
    mode: str | None
    project: str | None
    duration_s: int | None
    state: str
    title: str | None
    has_summary: bool
    has_transcript: bool
    todos_n: int


@dataclass(frozen=True, slots=True)
class RecDetail:
    id: str
    state: str
    meta: RecMeta | None
    result: RecResult | None
    summary: str | None
    truncated: bool
    summary_error: str | None = None  # why an existing summary.md was not read: E_TOO_LARGE, E_UNSAFE, E_IO


@dataclass(frozen=True, slots=True)
class Sentence:
    i: int
    t_s: float | None
    text: str


@dataclass(frozen=True, slots=True)
class TranscriptPage:
    sentences: tuple[Sentence, ...]
    total: int
    next_offset: int | None
    truncated: bool


@dataclass(frozen=True, slots=True)
class _Files:
    names: frozenset[str]
    manifest: Doc | None
    status: Doc | None
    result: Doc | None


# --- finding recordings -------------------------------------------------------------------------


def dates(root: Root, start: date | None = None, end: date | None = None) -> tuple[str, ...]:
    """Recording days in range, newest first."""
    names = fs.list_names(root, ("recordings",), kind="dir")
    return tuple(sorted((n for n in names if ids.is_date(n) and ids.in_range(n, start, end)), reverse=True))


def day_refs(root: Root, day: str) -> tuple[RecRef, ...]:
    """One day's recordings, newest first."""
    found = ((name, ids.split_rec_dir(name)) for name in fs.list_names(root, ("recordings", day), kind="dir"))
    refs = (RecRef(day, name, split[1]) for name, split in found if split is not None)
    return tuple(sorted(refs, key=lambda ref: ref.dir, reverse=True))


def _all(root: Root, start: date | None = None, end: date | None = None, after: str | None = None) -> Iterator[RecRef]:
    """Recordings in range, newest first; with `after`, only those below that key (newer days are not listed)."""
    days = (day for day in dates(root, start, end) if after is None or day <= after[:10])
    refs = chain.from_iterable(day_refs(root, day) for day in days)
    return refs if after is None else (ref for ref in refs if ref.key < after)


def scan(root: Root, start: date | None = None, end: date | None = None, hidden: frozenset[str] = frozenset(),
         max_dirs: int = SCAN_MAX, after: str | None = None) -> RecScan:  # fmt: skip
    """Recordings in range and below the key `after`, newest first, without hidden ones. Partial when more than
    max_dirs directories are left; `last` then says where to resume, so the cap counts from the resume point."""
    taken = tuple(islice(_all(root, start, end, after), max_dirs + 1))
    partial = len(taken) > max_dirs > 0
    return RecScan(tuple(r for r in taken[:max_dirs] if r.sid not in hidden), partial,
                   taken[max_dirs - 1].key if partial else None)  # fmt: skip


def find(root: Root, sid: str) -> RecRef:
    wanted = ids.parse_sid(sid)
    found = next((ref for ref in islice(_all(root), SCAN_MAX) if ref.sid == wanted), None)
    if found is None:
        raise ArchiveError("E_NOT_FOUND")
    return found


# --- reading the contract files -----------------------------------------------------------------


def _names(root: Root, ref: RecRef) -> frozenset[str]:
    try:
        return frozenset(fs.list_names(root, ref.parts, kind="file"))
    except ArchiveError:
        return frozenset()


def _json(root: Root, ref: RecRef, name: str, max_bytes: int, names: frozenset[str]) -> Any:
    if name not in names:
        return None
    try:
        return json.loads(fs.read_bytes(root, (*ref.parts, name), max_bytes))
    except (ArchiveError, ValueError, RecursionError):
        return None


def _same(doc: Any, ref: RecRef) -> bool:
    return isinstance(doc, Mapping) and isinstance(doc.get("session"), str) and doc["session"].lower() == ref.sid


def _manifest_ok(doc: Any, ref: RecRef) -> bool:
    return _same(doc, ref) and is_int(doc.get("v")) and doc["v"] == 1 and doc.get("mode") in MODES


def _accepted(check: Any, doc: Any, ref: RecRef) -> Doc | None:
    return doc if doc is not None and check(doc).ok and _same(doc, ref) else None


def _load(root: Root, ref: RecRef) -> _Files:
    names = _names(root, ref)
    manifest = _json(root, ref, "manifest.json", MANIFEST_MAX, names)
    status = _json(root, ref, "status.json", STATUS_MAX, names)
    result = _json(root, ref, "result.json", RESULT_MAX, names)
    return _Files(
        names,
        manifest if _manifest_ok(manifest, ref) else None,
        _accepted(contract.check_status, status, ref),
        _accepted(contract.check_result, result, ref),
    )


def _state(files: _Files) -> str:
    if files.result is not None:
        if files.result["state"] == "failed":
            return "failed"
        return "done" if files.result.get("level") == "summary" else "transcript"
    if files.status is not None:
        return "blocked" if files.status["state"] == "blocked" else "processing"
    return "archived" if files.manifest is not None else "pulling"


def _started(manifest: Doc) -> str | None:
    started = manifest.get("started")
    wall, tz_min = (started.get("wall_ms"), started.get("tz_min")) if isinstance(started, Mapping) else (None, None)
    if not is_int(wall) or not is_int(tz_min) or not -1440 < tz_min < 1440:
        return None
    try:
        when = datetime.fromtimestamp(wall / 1000, timezone(timedelta(minutes=tz_min)))
    except (OverflowError, OSError, ValueError):
        return None
    return when.isoformat(timespec="seconds")


def _project(manifest: Doc) -> str | None:
    project = manifest.get("project")
    key = project.get("key") if isinstance(project, Mapping) else None
    return key if matches(LIMITS["project_key_pattern"], key) else None


def _duration_s(manifest: Doc) -> int | None:
    value = manifest.get("duration_ms")
    return round(value / 1000) if is_int(value) and value >= 0 else None


def _secs(t_ms: Any) -> float | None:
    return round(t_ms / 1000, 1) if is_int(t_ms) and t_ms >= 0 else None


def row(root: Root, ref: RecRef) -> RecRow:
    files = _load(root, ref)
    manifest, result = files.manifest or {}, files.result or {}
    return RecRow(id=ref.id, date=ref.date, started=_started(manifest), mode=manifest.get("mode"),
                  project=_project(manifest), duration_s=_duration_s(manifest), state=_state(files),
                  title=result.get("title"), has_summary="summary.md" in files.names,
                  has_transcript=any(name in files.names for name in TRANSCRIPTS),
                  todos_n=len(result.get("actions") or ()))  # fmt: skip


# --- detail -------------------------------------------------------------------------------------


def _marks(raw: Any) -> tuple[Mark, ...]:
    marks = raw if isinstance(raw, list) else []
    good = (m for m in marks if isinstance(m, Mapping) and _secs(m.get("t_ms")) is not None
            and isinstance(m.get("k"), str) and MARK_K.match(m["k"]))  # fmt: skip
    return tuple(Mark(_secs(m["t_ms"]), m["k"]) for m in islice(good, MARKS_MAX))


def _meta(manifest: Doc | None) -> RecMeta | None:
    if manifest is None:
        return None
    segments = manifest.get("segments")
    return RecMeta(mode=manifest["mode"], started=_started(manifest), duration_s=_duration_s(manifest),
                   project=_project(manifest), segments=len(segments) if isinstance(segments, list) else 0,
                   marks=_marks(manifest.get("marks")),
                   consent="ack" if manifest.get("consent") == "ack" else None)  # fmt: skip


def _action(doc: Doc) -> Action:
    return Action(doc["n"], doc["text"], doc.get("owner"), doc.get("due"), _secs(doc.get("t_ms")), doc["confident"])


def _result(result: Doc | None) -> RecResult | None:
    if result is None:
        return None
    memo = result.get("memo")
    return RecResult(state=result["state"], level=result.get("level"), title=result.get("title"),
                     memo=Memo(memo["text"], memo["intent"]) if isinstance(memo, Mapping) else None,
                     actions=tuple(_action(a) for a in result.get("actions") or ()))  # fmt: skip


def read_summary(root: Root, ref: RecRef) -> str | None:
    """summary.md, or None when it is missing, unsafe or over its cap."""
    try:
        return fs.read_text(root, (*ref.parts, "summary.md"), SUMMARY_MAX)
    except ArchiveError:
        return None


def _summary(root: Root, ref: RecRef) -> tuple[str | None, str | None]:
    """(summary.md, None), or (None, the error code) when it exists but cannot be read."""
    try:
        return fs.read_text(root, (*ref.parts, "summary.md"), SUMMARY_MAX), None
    except ArchiveError as exc:
        return None, exc.code


def detail(root: Root, ref: RecRef, budget: int = BUDGET) -> RecDetail:
    files = _load(root, ref)
    summary, error = _summary(root, ref) if "summary.md" in files.names else (None, None)
    text, cut = clip(summary, budget) if summary is not None else (None, False)
    return RecDetail(ref.id, _state(files), _meta(files.manifest), _result(files.result), text, cut, error)


def project_of(root: Root, ref: RecRef) -> str | None:
    files = _load(root, ref)
    return _project(files.manifest) if files.manifest is not None else None


def tz_min(root: Root, ref: RecRef) -> int | None:
    """The manifest's started.tz_min (minutes east of UTC when the recording started), or None."""
    manifest = _json(root, ref, "manifest.json", MANIFEST_MAX, _names(root, ref))
    started = manifest.get("started") if _manifest_ok(manifest, ref) else None
    value = started.get("tz_min") if isinstance(started, Mapping) else None
    return value if is_int(value) and -1440 < value < 1440 else None


# --- transcript ---------------------------------------------------------------------------------

Line = tuple[float | None, str]


def _from_json(doc: Any, ref: RecRef) -> tuple[Line, ...] | None:
    if not _same(doc, ref) or not is_int(doc.get("v")) or doc["v"] != 1 or not isinstance(doc.get("sentences"), list):
        return None
    good = (s for s in doc["sentences"] if isinstance(s, Mapping) and isinstance(s.get("text"), str))
    return tuple((_secs(s.get("t_ms")), s["text"]) for s in good)


def _md_line(line: str) -> Line:
    stripped = line.strip()
    found = MD_LINE.match(stripped)
    if found is None:
        return None, stripped
    hours, minutes, seconds = (int(g) if g else 0 for g in found.group(1, 2, 3))
    text = found.group(4)
    block = MD_BLOCK.search(text)
    return float(hours * 3600 + minutes * 60 + seconds), text[: block.start()] if block else text


def sentences(root: Root, ref: RecRef) -> tuple[Line, ...]:
    """(seconds, text) from transcript.json, or from transcript.md when the JSON is missing or invalid."""
    names = _names(root, ref)
    parsed = _from_json(_json(root, ref, "transcript.json", TRANSCRIPT_MAX, names), ref)
    if parsed is not None:
        return parsed
    if "transcript.md" not in names:
        raise ArchiveError("E_NOT_FOUND")
    text = fs.read_text(root, (*ref.parts, "transcript.md"), TRANSCRIPT_MAX)
    return tuple(_md_line(line) for line in text.splitlines() if line.strip())


def transcript(root: Root, ref: RecRef, offset: int = 0, budget: int = BUDGET) -> TranscriptPage:
    lines = sentences(root, ref)
    chosen, after, truncated = take([text for _, text in lines], offset, budget, SENTENCE_COST)
    found = tuple(Sentence(offset + k, lines[offset + k][0], text) for k, text in enumerate(chosen))
    return TranscriptPage(found, len(lines), after if after < len(lines) else None, truncated)
