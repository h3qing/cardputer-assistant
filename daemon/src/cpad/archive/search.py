"""Search (docs/15 §3.4): a literal, case-insensitive substring over daily notes, summaries and transcripts.

No regular expressions over content (no ReDoS). Sources are read newest first: per day the daily note, then the
day's recordings newest first (summary, then transcript). A hit is a matching line or sentence with about CONTEXT
characters on each side; at most PER_SOURCE hits per source, each with the source's total match count, so one
long transcript cannot crowd out every other meeting. Every hit has a position (source key, index) and a page
that stops early says where the next one starts (`next`). Caps: FILES_MAX files, BYTES_MAX bytes, TIME_MAX_S
seconds per call; only a cap marks the result partial, and `next` then resumes the scan. Hidden recordings are
skipped.
"""

from __future__ import annotations

import re
import time
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass, replace
from datetime import date
from functools import partial

from cpad.archive import fs, ids, notes, recordings
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.recordings import RecRef

SCOPES = ("notes", "summaries", "transcripts")
FILES_MAX = 2000
BYTES_MAX = 64 << 20
TIME_MAX_S = 5.0
CONTEXT = 60
QUERY_MAX = 200
PER_SOURCE = 3
POS_RE = re.compile(r"(([0-9]{4}-[0-9]{2}-[0-9]{2})/(?:1|0/([0-9]{4}-[0-9a-z-]{18})/[01]))#([0-9])\Z")
Where = int | float | None
Lines = tuple[tuple[Where, str], ...]
Pos = tuple[str, int]  # (source key, how many of that source's hits come before)


@dataclass(frozen=True, slots=True)
class Hit:
    kind: str  # notes | summaries | transcripts
    ref: str  # the day of a daily note, or a recording id
    where: Where  # line number (1-based) in a note or summary; seconds into a recording for a transcript
    snippet: str
    i: int = 0  # 0-based line or sentence index: the offset read_day / transcript take to start at the hit
    matches: int = 1  # matching lines or sentences in the whole source (at most PER_SOURCE become hits)
    pos: Pos = ("", 0)  # where a page that starts with this hit resumes


@dataclass(frozen=True, slots=True)
class SearchResult:
    hits: tuple[Hit, ...]
    partial: bool  # a file, byte or time cap stopped the scan (`next` resumes it)
    scanned: int
    next: Pos | None = None  # where the next page starts; None when nothing in range is left


@dataclass(frozen=True, slots=True)
class _Source:
    kind: str
    ref: str
    key: str  # sources come in descending key order
    load: Callable[[], tuple[Lines, int]]  # (lines, bytes read)


@dataclass(frozen=True, slots=True)
class _Spent:
    deadline: float
    files: int = 0
    bytes: int = 0

    def add(self, size: int) -> _Spent:
        return replace(self, files=self.files + 1, bytes=self.bytes + size)

    def over(self, now: float) -> bool:
        return self.files >= FILES_MAX or self.bytes >= BYTES_MAX or now > self.deadline


def snippet(line: str, needle: str, context: int = CONTEXT) -> str | None:
    """The line around the first match, '…' where it was cut; None without a match."""
    folded = line.casefold()
    at = folded.find(needle.casefold())
    if at < 0:
        return None
    at = min(at, len(line))  # casefold may change lengths (ß -> ss): the cut is then approximate
    start, end = max(0, at - context), min(len(line), at + len(needle) + context)
    return ("…" if start else "") + line[start:end] + ("…" if end < len(line) else "")


def find_hits(text: str, needle: str, context: int = CONTEXT) -> tuple[tuple[int, str], ...]:
    """(1-based line number, snippet) of every line that contains the needle. Pure."""
    found = ((n, snippet(line, needle, context)) for n, line in enumerate(text.splitlines(), start=1))
    return tuple((n, snip) for n, snip in found if snip is not None)


def _text_lines(root: Root, parts: tuple[str, ...], max_bytes: int) -> tuple[Lines, int]:
    raw = fs.read_bytes(root, parts, max_bytes)
    lines = raw.decode("utf-8", errors="replace").splitlines()
    return tuple(enumerate(lines, start=1)), len(raw)


def _transcript_lines(root: Root, ref: RecRef) -> tuple[Lines, int]:
    lines = recordings.sentences(root, ref)
    return lines, sum(len(text.encode("utf-8")) for _, text in lines)


def format_pos(pos: Pos) -> str:
    return f"{pos[0]}#{pos[1]}"


def parse_pos(text: object) -> Pos:
    """A position made by format_pos; ValueError for anything else."""
    found = POS_RE.match(text) if isinstance(text, str) else None
    valid = found is not None and ids.is_date(found.group(2)) and int(found.group(4)) < PER_SOURCE
    if not valid or (found.group(3) is not None and ids.split_rec_dir(found.group(3)) is None):
        raise ValueError("invalid search position")
    return found.group(1), int(found.group(4))


def _rec_sources(root: Root, ref: RecRef, scopes: Sequence[str]) -> Iterator[_Source]:
    key = f"{ref.date}/0/{ref.dir}"
    if "summaries" in scopes:
        yield _Source("summaries", ref.id, f"{key}/1",
                      partial(_text_lines, root, (*ref.parts, "summary.md"), recordings.SUMMARY_MAX))  # fmt: skip
    if "transcripts" in scopes:
        yield _Source("transcripts", ref.id, f"{key}/0", partial(_transcript_lines, root, ref))


def _sources(root: Root, scopes: Sequence[str], start: date | None, end: date | None,
             hidden: frozenset[str]) -> Iterator[_Source]:  # fmt: skip
    noted = frozenset(notes.note_days(root, start, end)) if "notes" in scopes else frozenset()
    recorded = frozenset(recordings.dates(root, start, end)) if set(scopes) - {"notes"} else frozenset()
    for day in sorted(noted | recorded, reverse=True):
        if day in noted:
            yield _Source("notes", day, f"{day}/1", partial(_text_lines, root, notes.note_parts(day), notes.NOTE_MAX))
        refs = recordings.day_refs(root, day) if day in recorded else ()
        for ref in (r for r in refs if r.sid not in hidden):
            yield from _rec_sources(root, ref, scopes)


def _load(source: _Source) -> tuple[Lines, int] | None:
    try:
        return source.load()
    except ArchiveError:
        return None


def _check(query: str, scopes: Sequence[str]) -> None:
    if not isinstance(query, str) or not 0 < len(query) <= QUERY_MAX:
        raise ValueError("query must be 1 to 200 characters")
    if not scopes or not set(scopes) <= set(SCOPES):
        raise ValueError("scopes must be some of notes, summaries, transcripts")


def _resumed(root: Root, scopes: Sequence[str], start: date | None, end: date | None, hidden: frozenset[str],
             after: Pos | None) -> Iterator[_Source]:  # fmt: skip
    """Sources from `after` on; newer days are not even listed."""
    if after is None:
        return _sources(root, scopes, start, end, hidden)
    day = ids.parse_date(after[0][:10])
    last = day if end is None or day < end else end
    return (source for source in _sources(root, scopes, start, last, hidden) if source.key <= after[0])


def _source_hits(source: _Source, lines: Lines, needle: str) -> tuple[Hit, ...]:
    found = ((n, where, snippet(text, needle)) for n, (where, text) in enumerate(lines))
    matched = tuple((n, where, snip) for n, where, snip in found if snip is not None)
    return tuple(Hit(source.kind, source.ref, where, snip, n, len(matched), (source.key, k))
                 for k, (n, where, snip) in enumerate(matched[:PER_SOURCE]))  # fmt: skip


def search(root: Root, query: str, scopes: Sequence[str] = SCOPES, start: date | None = None,
           end: date | None = None, limit: int = 20, clock: Callable[[], float] = time.monotonic,
           hidden: frozenset[str] = frozenset(), after: Pos | None = None) -> SearchResult:  # fmt: skip
    """At most `limit` hits from position `after` on. `next` is set when a further hit exists or a cap stopped
    the scan (then partial); a full page looks ahead to the next hit so that `next` never leads nowhere."""
    _check(query, scopes)
    spent = _Spent(clock() + TIME_MAX_S)
    hits: tuple[Hit, ...] = ()
    for source in _resumed(root, scopes, start, end, hidden, after):
        if spent.over(clock()):
            return SearchResult(hits, True, spent.files, (source.key, 0))
        loaded = _load(source)
        if loaded is None:
            continue
        spent = spent.add(loaded[1])
        skip = after[1] if after is not None and source.key == after[0] else 0
        found = _source_hits(source, loaded[0], query)[skip:]
        room = limit - len(hits)
        if len(found) > room:
            return SearchResult(hits + found[:room], False, spent.files, found[room].pos)
        hits += found
    return SearchResult(hits, False, spent.files)
