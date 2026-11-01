"""The production Reader and Writer behind cpa mcp (docs/15 §3.4, §3.6, §7.1).

ArchiveReader turns cpad.archive's typed results into plain rows with their page keys. Recording rows
are built lazily, one directory at a time, so a page reads only what it returns. Hidden recordings
(rec.hidden, v0.4.1) are invisible here, their memo captures and the queue items handed from them included. A
recording's project, its todos' and its memo capture's, is the effective one of the filing fold (docs/23 §2.1), as
`cpa rec ls` shows it; one the log does not know keeps its manifest project; its env is the Mac-side stamp of its
rec.archived (host and link). list_unfiled and get_filing_item are `cpa filing ls` and `show` (cpad.filing.view),
with MCP's argument names (query for text) and error codes. EventsWriter appends queue.added exactly as
`cpa queue add` does, with origin "mcp"; nothing is ever executed.
"""

from __future__ import annotations

import time
from collections.abc import Callable, Iterator, Mapping
from dataclasses import asdict, dataclass
from datetime import date
from functools import partial
from typing import Any

from cpad.archive import events_view, notes, recordings, search, status, todos
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.recordings import RecRef
from cpad.config.settings import Paths
from cpad.events import records
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.filing.args import E_UNKNOWN_ID, E_UNKNOWN_PROJECT, FilingError
from cpad.mcp.tools import MESSAGES, Ctx, Keyed, Listing, ToolError
from cpad.projects.registry import ProjectsError, find, load_registry
from cpad.queue.fold import assign_qids, fold_queue

FILTERS = ("mode", "project", "state")
SCAN_DIRS = recordings.SCAN_MAX  # recording directories per list_recordings call
STATE_NAMES = {"archived": "waiting"}  # the MCP word for "audio saved, transcriber not started" (as get_status)
FILING_NAMES = {"query": "text"}  # list_unfiled's argument -> its cpa filing ls name (MCP search calls it query)
FILING_FIELDS = {v: k for k, v in FILING_NAMES.items()}
FILING_CODES = {E_UNKNOWN_ID: "E_NOT_FOUND", E_UNKNOWN_PROJECT: "E_BAD_ARG"}


def _state(state: str) -> str:
    return STATE_NAMES.get(state, state)


def _matches(row: Mapping[str, Any], a: Mapping[str, Any]) -> bool:
    return all(a.get(k) is None or row.get(k) == a[k] for k in FILTERS)


def _todo(t: todos.Todo) -> dict[str, Any]:
    where = "date" if t.source_kind == "note" else "id"
    return {"text": t.text, "done": t.done, "date": t.date, "project": t.project,
            "source": {"kind": t.source_kind, where: t.source_id, "line": t.line}, "block": t.block}  # fmt: skip


def _of_hidden(c: events_view.Capture, hidden: frozenset[str]) -> bool:
    """A memo capture is its recording's text: hidden with it (hidden holds lower-case session ids)."""
    return c.kind == "memo" and c.id.lower() in hidden


def _hit(h: search.Hit) -> dict[str, Any]:
    """A hit and where to read on: i (a sentence) for read_transcript, offset (line - 1) for read_daily_note."""
    source = {"kind": h.kind, ("date" if h.kind == "notes" else "id"): h.ref, "matches": h.matches}
    found = {"source": source, "snippet": h.snippet, "pos": search.format_pos(h.pos)}
    if h.kind == "transcripts":
        return {**found, "t_s": h.where, "i": h.i}
    return {**found, "line": h.where} | ({"offset": h.i} if h.kind == "notes" else {})


def _filing_raw(a: Mapping[str, Any]) -> dict[str, Any]:
    named = {FILING_NAMES.get(k, k): v for k, v in a.items()}
    return {k: v.isoformat() if isinstance(v, date) else v for k, v in named.items()}


def _refused(err: FilingError) -> ToolError:
    """A refused ls or show in MCP's words: a bad argument names the MCP argument, an unknown id is not found."""
    code = FILING_CODES.get(err.code, err.code)
    if code == "E_BAD_ARG":
        return ToolError(code, FILING_FIELDS.get(err.field, err.field))
    return ToolError(code if code in MESSAGES else "E_UNAVAILABLE")


def _position(after: str | None) -> search.Pos | None:
    try:
        return None if after is None else search.parse_pos(after)
    except ValueError as exc:
        raise ToolError("E_BAD_ARG", "cursor") from exc


@dataclass(frozen=True, slots=True)
class ArchiveReader:
    paths: Paths
    root: Root

    def _events(self) -> tuple[dict, ...]:
        return EventLog(self.paths.events).load()

    def _hidden(self) -> frozenset[str]:
        return events_view.hidden(self._events())

    def _book(self) -> view.Book:
        return view.Book.of(self._events(), view.known(self.paths.projects))

    def _projects(self) -> Mapping[str, str | None]:
        """Lower-case session id -> effective project."""
        return self._book().by_sid()

    def _ref(self, sid: str) -> RecRef:
        ref = recordings.find(self.root, sid)
        if ref.sid in self._hidden():
            raise ArchiveError("E_NOT_FOUND")
        return ref

    def days(self, a: Mapping[str, Any], after: str | None) -> list[Keyed]:
        found = notes.list_days(self.root, a.get("from"), a.get("to"), self._hidden())
        return [Keyed(d.date, asdict(d)) for d in found if after is None or d.date < after]

    def day(self, day: date, offset: int, budget: int) -> dict[str, Any]:
        page = notes.read_day(self.root, day, offset, budget)
        return {"date": page.date, "text": page.text, "total_lines": page.total_lines, "next_offset": page.next_offset}

    def recordings(self, a: Mapping[str, Any], after: str | None) -> Listing:
        found = recordings.scan(self.root, a.get("from"), a.get("to"), self._hidden(), SCAN_DIRS, after)
        return Listing(self._rows(found.refs, a, self._book()), found.partial, found.last)

    def _rows(self, refs: tuple[RecRef, ...], a: Mapping[str, Any], book: view.Book) -> Iterator[Keyed]:
        projects, envs = book.by_sid(), book.envs()
        for ref in refs:
            found = asdict(recordings.row(self.root, ref))
            row = {**found, "state": _state(found["state"]), "project": projects.get(ref.sid, found["project"]),
                   "env": envs.get(ref.sid)}  # fmt: skip
            if _matches(row, a):
                yield Keyed(ref.key, row)

    def recording(self, sid: str, budget: int) -> dict[str, Any]:
        ref = self._ref(sid)
        d = recordings.detail(self.root, ref, budget)
        meta = asdict(d.meta) if d.meta else {}
        book = self._book()
        projects = book.by_sid()
        meta = {**meta, "project": projects[ref.sid]} if meta and ref.sid in projects else meta
        meta = {**meta, "env": book.envs().get(ref.sid)} if meta else meta
        return {"id": d.id, "state": _state(d.state), "meta": meta,
                "result": asdict(d.result) if d.result else {},
                "summary": {"text": d.summary or "", "truncated": d.truncated}
                | ({"error": d.summary_error} if d.summary_error else {})}  # fmt: skip

    def transcript(self, sid: str, offset: int, budget: int) -> dict[str, Any]:
        page = recordings.transcript(self.root, self._ref(sid), offset, budget)
        return {"sentences": [asdict(s) for s in page.sentences], "total": page.total,
                "next_offset": page.next_offset}  # fmt: skip

    def todos(self, a: Mapping[str, Any], after: str | None) -> Listing:
        page = todos.collect(self.root, self._hidden(), a.get("from"), a.get("to"), a.get("status") or "open",
                             a.get("project"), after=after, limit=a["limit"] + 1,
                             projects=self._projects())  # fmt: skip
        return Listing([Keyed(t.key, _todo(t)) for t in page.items], page.partial, page.next_key)

    def captures(self, a: Mapping[str, Any], after: str | None) -> list[Keyed]:
        events = self._events()
        projects = view.Book.of(events, view.known(self.paths.projects)).projects()
        found = events_view.captures(events, a.get("from"), a.get("to"), a.get("kind"), a.get("project"),
                                     projects=projects)  # fmt: skip
        hidden = events_view.hidden(events)
        shown = (c for c in found if not _of_hidden(c, hidden))
        return [Keyed(c.key, asdict(c)) for c in shown if after is None or c.key < after]

    def queue(self, project: str | None) -> list[Keyed]:
        events = self._events()
        items = events_view.queue_rows(events, project, hidden=events_view.hidden(events))
        return [Keyed(f"{int(i.qid[1:]):011d}", {"qid": i.qid, "project": i.project, "intent": i.intent,
                                                 "text": i.text, "origin": i.origin, "uncertain": i.uncertain})
                for i in items]  # fmt: skip

    def search(self, query: str, scopes: tuple[str, ...], start: date | None, end: date | None, limit: int,
               after: str | None) -> dict[str, Any]:  # fmt: skip
        found = search.search(self.root, query, scopes, start, end, limit, hidden=self._hidden(),
                              after=_position(after))  # fmt: skip
        return {"hits": [_hit(h) for h in found.hits], "partial": found.partial, "scanned": found.scanned,
                "next": search.format_pos(found.next) if found.next else None}  # fmt: skip

    def unfiled(self, a: Mapping[str, Any]) -> dict[str, Any]:
        """One page of `cpa filing ls --json`: the filing argument has no hidden, so no hidden row is ever in it."""
        try:
            return view.listing(self.paths, self.root, _filing_raw(a), view.on_device(self.paths))
        except FilingError as err:
            raise _refused(err) from None

    def filing_item(self, sid: str) -> dict[str, Any]:
        """`cpa filing show --json` of one recording; a hidden one is not found."""
        try:
            doc = view.detail(self.paths, self.root, sid)
        except FilingError as err:
            raise _refused(err) from None
        if doc["row"]["filing"] == "hidden":
            raise ToolError("E_NOT_FOUND")
        return doc


@dataclass(frozen=True, slots=True)
class EventsWriter:
    paths: Paths
    clock: Callable[[], float] = time.time

    def _known(self, project: str) -> None:
        try:
            registered = find(load_registry(self.paths.projects), project)
        except ProjectsError as exc:
            raise ToolError("E_UNAVAILABLE") from exc
        if registered is None:
            raise ToolError("E_BAD_ARG", "project")

    def queue_add(self, project: str, text: str) -> str:
        """The same record and qid allocation as `cpa queue add`, under the events.jsonl lock."""
        self._known(project)
        log = EventLog(self.paths.events)
        try:
            state = fold_queue(log.load())
            rec = (records.queue_added(qid=None, project=project, text=text, intent="todo", origin="mcp",
                                       at=records.iso(self.clock())),)  # fmt: skip
            _, written = log.append(rec, prepare=lambda new: assign_qids(rec, fold_queue(new, state)))
        except OSError as exc:
            raise ToolError("E_UNAVAILABLE") from exc
        return written[0]["qid"]


def context(paths: Paths, root: Root, writes: bool = False) -> Ctx:
    return Ctx(reader=ArchiveReader(paths, root), status=partial(status.status, paths, root),
               writer=EventsWriter(paths) if writes else None)  # fmt: skip
