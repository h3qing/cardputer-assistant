"""Todos (docs/15 §3.4): checkbox lines in daily notes and in recordings' summary.md. Read-only.

Newest first: by day; within a day recordings (newest first, their tasks in document order) before the
daily note (its later lines first, as cpad appends). A summary task without a #tag gets the recording's
project: its effective project (docs/23 §2.1) when `projects` has it, else the manifest's. A daily-note task
that is a recording's own line (block ^cpa-<session id>) takes its effective project too, since filing it later
appends a '↪ Filed' line and never edits that one; a typed note keeps its #tag (its id is never a session's).
Days are read newest first, from the cursor's day on, and reading stops once the page is full; at most FILES_MAX
files per call. A page cut short by that cap is partial and its next_key ("<day>/~", just above every key of that day)
resumes at the first day it did not read.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from datetime import date
from itertools import chain

from cpad.archive import notes, recordings
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.paging import Page, page
from cpad.archive.recordings import RecRef

FILES_MAX = 2000
Projects = Mapping[str, str | None]  # lower-case session id -> effective project (filing.view.Book.by_sid)
STATUSES = ("open", "done", "all")
LINE_MAX = 999_999


@dataclass(frozen=True, slots=True)
class Todo:
    text: str
    done: bool
    date: str
    project: str | None
    source_kind: str  # note | recording
    source_id: str  # the date of the daily note, or the recording id
    block: str | None
    line: int
    key: str  # newest-first sort and page key


def _note_key(day: str, line: int) -> str:
    return f"{day}/n/{line:06d}"


def _rec_key(ref: RecRef, line: int) -> str:
    return f"{ref.date}/r/{ref.dir}/{LINE_MAX - line:06d}"


def _note_project(task: notes.Task, projects: Projects) -> str | None:
    sid = task.block[4:] if task.block and task.block.startswith("cpa-") else None
    return projects.get(sid, task.project)


def _note_todos(root: Root, day: str, projects: Projects) -> tuple[Todo, ...]:
    try:
        text = notes.read_note(root, day)
    except ArchiveError:
        return ()
    return tuple(Todo(t.text, t.done, day, _note_project(t, projects), "note", day, t.block, t.line,
                      _note_key(day, t.line)) for t in notes.parse_tasks(text))  # fmt: skip


def _rec_todos(root: Root, ref: RecRef, projects: Projects) -> tuple[Todo, ...]:
    text = recordings.read_summary(root, ref)
    tasks = notes.parse_tasks(text) if text is not None else ()
    known = ref.sid in projects
    project = (projects[ref.sid] if known else recordings.project_of(root, ref)) if tasks else None
    return tuple(Todo(t.text, t.done, ref.date, t.project or project, "recording", ref.id, t.block, t.line,
                      _rec_key(ref, t.line)) for t in tasks)  # fmt: skip


def _day(root: Root, day: str, noted: frozenset[str], hidden: frozenset[str],
         projects: Projects) -> tuple[tuple[Todo, ...], int]:  # fmt: skip
    """(the day's todos, how many files were opened)."""
    refs = tuple(ref for ref in recordings.day_refs(root, day) if ref.sid not in hidden)
    from_recs = tuple(chain.from_iterable(_rec_todos(root, ref, projects) for ref in refs))
    from_note = _note_todos(root, day, projects) if day in noted else ()
    return from_recs + from_note, len(refs) + (day in noted)


def _wanted(todo: Todo, status: str, project: str | None, after: str | None) -> bool:
    status_ok = status == "all" or todo.done == (status == "done")
    return status_ok and project in (None, todo.project) and (after is None or todo.key < after)


def collect(root: Root, hidden: frozenset[str] = frozenset(), start: date | None = None, end: date | None = None,
            status: str = "open", project: str | None = None, *, after: str | None = None,
            limit: int | None = None, projects: Projects | None = None) -> Page[Todo]:  # fmt: skip
    """Todos newest first, after the key `after`; at most `limit` of them (all when None)."""
    if status not in STATUSES:
        raise ValueError("status must be open, done or all")
    noted = frozenset(notes.note_days(root, start, end))
    days = sorted(noted | frozenset(recordings.dates(root, start, end)), reverse=True)
    found: tuple[Todo, ...] = ()
    opened, stopped = 0, None
    for day in (d for d in days if after is None or d <= after[:10]):
        if opened >= FILES_MAX:
            stopped = day
            break
        todos, read = _day(root, day, noted, hidden, projects or {})
        opened += read
        found += tuple(t for t in todos if _wanted(t, status, project, after))
        if limit is not None and len(found) > limit:
            break
    ordered = sorted(found, key=lambda t: t.key, reverse=True)
    chosen = page(ordered, lambda t: t.key, None, len(ordered) if limit is None else limit)
    return chosen if stopped is None else Page(chosen.items, f"{stopped}/~", True)
