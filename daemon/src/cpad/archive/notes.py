"""Daily notes in the archive (docs/15 §2.1, §3.4): `<YYYY-MM-DD>.md` at the archive root. Read-only.

cpad only appends to them; the user may edit them (Obsidian), so they are read as UTF-8 with errors
replaced. Tasks are Markdown checkboxes, `- [ ] …` / `- [x] …`, as cpad, the transcriber and the user write
them; a trailing block id (`^cpa-<id>`, `^act-<n>`) and the first `#<project key>` tag are taken apart.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import date

from cpad.archive import fs, ids, recordings
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.paging import BUDGET, take

NOTE_MAX = 2 << 20
NOTE_NAME = re.compile(r"([0-9]{4}-[0-9]{2}-[0-9]{2})\.md\Z")
TASK = re.compile(r"\s*[-*+] \[([ xX])\] (.*\S)\s*\Z")
BLOCK = re.compile(r"(?:^|\s)\^([A-Za-z0-9-]+)\Z")
TAG = re.compile(r"(?<!\S)#([a-z0-9][a-z0-9-]{0,11})(?!\S)")


@dataclass(frozen=True, slots=True)
class DayInfo:
    date: str
    bytes: int  # size of the daily note; 0 when the day only has recordings
    recordings: int


@dataclass(frozen=True, slots=True)
class DayPage:
    date: str
    text: str
    total_lines: int
    next_offset: int | None
    truncated: bool


@dataclass(frozen=True, slots=True)
class Task:
    text: str
    done: bool
    project: str | None
    block: str | None
    line: int  # 1-based


def note_parts(day: str) -> tuple[str, ...]:
    return (f"{day}.md",)


def note_days(root: Root, start: date | None = None, end: date | None = None) -> tuple[str, ...]:
    """Days with a daily note in range, newest first."""
    found = (NOTE_NAME.match(name) for name in fs.list_names(root, (), kind="file"))
    days = (m.group(1) for m in found if m is not None)
    return tuple(sorted((d for d in days if ids.is_date(d) and ids.in_range(d, start, end)), reverse=True))


def _size(root: Root, day: str) -> int:
    try:
        return fs.file_size(root, note_parts(day))
    except ArchiveError:
        return 0


def list_days(root: Root, start: date | None = None, end: date | None = None,
              hidden: frozenset[str] = frozenset()) -> tuple[DayInfo, ...]:  # fmt: skip
    """Days with a daily note or recordings, newest first."""
    noted = frozenset(note_days(root, start, end))
    days = sorted(noted | frozenset(recordings.dates(root, start, end)), reverse=True)
    return tuple(
        DayInfo(day, _size(root, day) if day in noted else 0,
                sum(1 for ref in recordings.day_refs(root, day) if ref.sid not in hidden))
        for day in days
    )  # fmt: skip


def read_note(root: Root, day: str) -> str:
    return fs.read_text(root, note_parts(day), NOTE_MAX)


def read_day(root: Root, day: date, offset: int = 0, budget: int = BUDGET) -> DayPage:
    """Whole lines from `offset` that fit in `budget` bytes; next_offset continues where this page stopped."""
    lines = read_note(root, day.isoformat()).splitlines()
    chosen, after, truncated = take(lines, offset, budget, 1)
    return DayPage(day.isoformat(), "\n".join(chosen), len(lines), after if after < len(lines) else None, truncated)


def _task(line: str, number: int) -> Task | None:
    found = TASK.match(line)
    if found is None:
        return None
    body = found.group(2)
    block = BLOCK.search(body)
    text = body[: block.start()].strip() if block else body
    if not text:
        return None
    tag = TAG.search(text)
    return Task(text, found.group(1) != " ", tag.group(1) if tag else None, block.group(1) if block else None, number)


def parse_tasks(text: str) -> tuple[Task, ...]:
    found = (_task(line, number) for number, line in enumerate(text.splitlines(), start=1))
    return tuple(task for task in found if task is not None)
