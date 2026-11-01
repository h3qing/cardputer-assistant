"""Tool handlers (docs/15 §3.4, §3.6): validated arguments in, a bounded, marked result or an error code out.

Data comes only through the Reader seam (backed by cpad.archive in production, a fake in tests).
Every structured result carries trust = "untrusted-user-data" and fits OUTPUT_MAX; lists are paged by
key with opaque cursors. Errors are a code, a fixed English message and at most the field name.
list_unfiled and get_filing_item (docs/23 §3.5) answer with the filing contract's documents: a page cut to the
budget goes on after its last row (the cursor of `cpa filing ls`), a cut transcript at the next sentence
(read_transcript's offset).
"""

from __future__ import annotations

import itertools
from collections.abc import Callable, Iterable, Iterator, Mapping
from dataclasses import dataclass, field
from datetime import date
from types import MappingProxyType
from typing import Any, Protocol

from cpad.archive.fs import ArchiveError
from cpad.archive.ids import BadId, decode_cursor, encode_cursor
from cpad.filing.query import cursor_after
from cpad.mcp import args as tool_args
from cpad.mcp import shapes
from cpad.mcp.limits import OUTPUT_MAX, fit, json_size
from cpad.mcp.schemas import TRUST
from cpad.protocol.validate import utf8_len

TEXT_BUDGET = OUTPUT_MAX - 1024  # room for the envelope around a page of text
SHRINK_TRIES = 4
MESSAGES: Mapping[str, str] = MappingProxyType({
    "E_BAD_ARG": "An argument is invalid",
    "E_NOT_FOUND": "Not found",
    "E_TOO_LARGE": "Too large to return",
    "E_UNSAFE": "Refused: not a plain file inside the archive",
    "E_RATE_LIMITED": "Too many calls; retry later",
    "E_UNAVAILABLE": "The archive could not be read",
    "E_AUDIT": "The audit log could not be written, so nothing was done or returned",
})  # fmt: skip
ID_ARGS = ("date", "from", "to", "id", "project")


@dataclass(frozen=True, slots=True)
class Keyed:
    """One list row and its sort key (newest first = descending keys)."""

    key: str
    row: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class Listing:
    """Rows from a scan with a cap: partial when the cap stopped it, and then `resume` is the key to continue below.
    Iterating a Listing iterates its rows."""

    rows: Iterable[Keyed]
    partial: bool = False
    resume: str | None = None

    def __iter__(self) -> Iterator[Keyed]:
        return iter(self.rows)


class Reader(Protocol):
    def days(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]: ...
    def day(self, day: date, offset: int, budget: int) -> Mapping[str, Any]: ...
    def recordings(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]: ...  # may be a Listing
    def recording(self, sid: str, budget: int) -> Mapping[str, Any]: ...
    def transcript(self, sid: str, offset: int, budget: int) -> Mapping[str, Any]: ...
    def todos(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]: ...  # may be a Listing
    def captures(self, a: Mapping[str, Any], after: str | None) -> Iterable[Keyed]: ...
    def queue(self, project: str | None) -> Iterable[Keyed]: ...
    def search(self, query: str, scopes: tuple[str, ...], start: date | None, end: date | None, limit: int,
               after: str | None) -> Mapping[str, Any]: ...  # fmt: skip
    def unfiled(self, a: Mapping[str, Any]) -> Mapping[str, Any]: ...  # cpa filing ls --json
    def filing_item(self, sid: str) -> Mapping[str, Any]: ...  # cpa filing show --json


class Writer(Protocol):
    def queue_add(self, project: str, text: str) -> str: ...


@dataclass(frozen=True, slots=True)
class Ctx:
    reader: Reader
    status: Callable[[], Mapping[str, Any]]
    writer: Writer | None = None


class ToolError(Exception):
    def __init__(self, code: str, field: str | None = None, retry_after_s: float | None = None):
        super().__init__(code)
        self.code, self.field, self.retry_after_s = code, field, retry_after_s

    def body(self) -> dict[str, Any]:
        err: dict[str, Any] = {"code": self.code, "message": MESSAGES[self.code]}
        if self.field is not None:
            err["field"] = self.field
        if self.retry_after_s is not None:
            err["retry_after_s"] = self.retry_after_s
        return {"error": err}


@dataclass(frozen=True, slots=True)
class Outcome:
    body: Mapping[str, Any] | None = None
    error: ToolError | None = None
    n: int | None = None
    ids: tuple[str, ...] = ()
    sizes: Mapping[str, int] = field(default_factory=lambda: MappingProxyType({}))


def _after(kind: str, a: Mapping[str, Any]) -> str | None:
    try:
        return decode_cursor(kind, a["cursor"]) if a.get("cursor") is not None else None
    except BadId as exc:
        raise ToolError("E_BAD_ARG", "cursor") from exc


def paged(kind: str, rows: Iterable[Keyed], limit: int, cursored: bool = True) -> tuple[dict[str, Any], int]:
    """One page of rows: at most `limit`, cut to the budget, with next_cursor when more follow or when a scan cap
    stopped the listing (partial) and the reader said where to resume."""
    found = rows if isinstance(rows, Listing) else Listing(rows)
    it = iter(found)
    head = list(itertools.islice(it, limit))
    more = next(it, None) is not None
    kept, cut = fit((shapes.pick(kind, k.row) for k in head), {"trust": TRUST}, kind)
    if cut and not kept:
        raise ToolError("E_TOO_LARGE")
    body: dict[str, Any] = {"trust": TRUST, kind: list(kept)}
    resume = head[len(kept) - 1].key if more or cut else found.resume if found.partial else None
    if resume is not None and cursored:
        body["next_cursor"] = encode_cursor(kind, resume)
    if cut or (more and not cursored):
        body["truncated"] = True
    return body | ({"partial": True} if found.partial else {}), len(kept)


def _list(kind: str, fetch: Callable[[Reader], Callable[..., Iterable[Keyed]]]) -> Callable:
    def handler(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
        return paged(kind, fetch(ctx.reader)(a, _after(kind, a)), a["limit"])

    return handler


def _text_page(doc: Mapping[str, Any], key: str, extra: Mapping[str, Any]) -> dict[str, Any]:
    body = {"trust": TRUST, **extra, key: doc.get(key, "")}
    if doc.get("total_lines") is not None:
        body["total_lines"] = doc["total_lines"]
    if doc.get("next_offset") is not None:
        body |= {"next_offset": doc["next_offset"], "truncated": True}
    return body


def within(build: Callable[[int], dict[str, Any]], blank: Callable[[dict[str, Any]], dict[str, Any]]) -> dict:
    """Build a text page; when JSON escaping (newlines, quotes, control characters) or the fixed fields push it
    over the output cap, build it again with the text budget scaled to the room left. `blank` empties the text
    part, so the fixed part can be measured. A shorter page stays resumable through its offset."""
    budget = TEXT_BUDGET
    for _ in range(SHRINK_TRIES):
        body = build(budget)
        size, fixed = json_size(body), json_size(blank(body))
        if size <= OUTPUT_MAX:
            return body
        room = OUTPUT_MAX - fixed - 256
        if room <= 0:
            break
        budget = max(1, int(budget * room / max(1, size - fixed) * 0.95))
    raise ToolError("E_TOO_LARGE")


def _read_daily_note(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    def build(budget: int) -> dict[str, Any]:
        return _text_page(ctx.reader.day(a["date"], a["offset"], budget), "text", {"date": a["date"].isoformat()})

    return within(build, lambda body: {**body, "text": ""}), 1


def _get_recording(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    def build(budget: int) -> dict[str, Any]:
        return {"trust": TRUST, **shapes.detail({"id": a["id"].upper(), **ctx.reader.recording(a["id"], budget)})}

    return within(build, lambda body: {**body, "summary": {**body["summary"], "text": ""}}), 1


def _transcript_page(doc: Mapping[str, Any], sid: str) -> dict[str, Any]:
    sentences = [shapes.pick("sentences", s) for s in doc.get("sentences", ())]
    body = {"trust": TRUST, "id": sid.upper(), "sentences": sentences, "total": doc.get("total", len(sentences))}
    if doc.get("next_offset") is not None:
        body |= {"next_offset": doc["next_offset"], "truncated": True}
    return body


def _read_transcript(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    def build(budget: int) -> dict[str, Any]:
        return _transcript_page(ctx.reader.transcript(a["id"], a["offset"], budget), a["id"])

    body = within(build, lambda body: {**body, "sentences": []})
    return body, len(body["sentences"])


def _list_queue(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    return paged("items", ctx.reader.queue(a["project"]), a["limit"], cursored=False)


def _search(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    """A page of hits. next_cursor: where the next page starts (the first hit the output cap left out, or the
    reader's own next position); partial: only the reader's scan caps."""
    found = ctx.reader.search(a["query"], a["in"], a["from"], a["to"], a["limit"], _after("hits", a))
    raw = list(found.get("hits", ()))[: a["limit"]]
    kept, cut = fit((shapes.pick("hits", h) for h in raw), {"trust": TRUST, "partial": True, "scanned": 0}, "hits")
    body = {"trust": TRUST, "hits": list(kept), "partial": bool(found.get("partial")),
            "scanned": int(found.get("scanned", 0))}  # fmt: skip
    resume = raw[len(kept)].get("pos") if cut else found.get("next")
    body |= {"next_cursor": encode_cursor("hits", resume)} if resume else {}
    return (body | {"truncated": True} if cut else body), len(kept)


def _list_unfiled(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    page = shapes.filing_page(ctx.reader.unfiled(a))
    head = {"trust": TRUST, **page, "rows": []}
    kept, cut = fit(page["rows"], head, "rows")
    if cut and not kept:
        raise ToolError("E_TOO_LARGE")
    if not cut:
        return {**head, "rows": page["rows"]}, len(kept)
    return {**head, "next_cursor": cursor_after(kept[-1]), "rows": list(kept), "truncated": True}, len(kept)


def _get_filing_item(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    doc = {"trust": TRUST, **shapes.filing_item(ctx.reader.filing_item(a["id"]))}
    kept, cut = fit(doc["sentences"] or (), {**doc, "sentences": []}, "sentences")
    return ({**doc, "sentences": list(kept), "next_offset": len(kept), "truncated": True} if cut else doc), 1


def _get_status(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    return {"trust": TRUST, **shapes.status(ctx.status())}, 1


def _queue_add(ctx: Ctx, a: Mapping[str, Any]) -> tuple[dict[str, Any], int]:
    if ctx.writer is None:
        raise ToolError("E_UNAVAILABLE")
    qid = ctx.writer.queue_add(a["project"], a["text"])
    return {"trust": TRUST, "qid": qid, "project": a["project"]}, 1


HANDLERS: Mapping[str, Callable[[Ctx, Mapping[str, Any]], tuple[dict[str, Any], int]]] = MappingProxyType({
    "list_days": _list("days", lambda r: r.days),
    "read_daily_note": _read_daily_note,
    "list_recordings": _list("recordings", lambda r: r.recordings),
    "get_recording": _get_recording,
    "read_transcript": _read_transcript,
    "list_todos": _list("todos", lambda r: r.todos),
    "list_captures": _list("captures", lambda r: r.captures),
    "list_queue": _list_queue,
    "get_status": _get_status,
    "search": _search,
    "queue_add": _queue_add,
    "list_unfiled": _list_unfiled,
    "get_filing_item": _get_filing_item,
})  # fmt: skip


def audit_ids(a: Mapping[str, Any]) -> tuple[str, ...]:
    values = (a.get(k) for k in ID_ARGS)
    return tuple(v.isoformat() if isinstance(v, date) else v for v in values if isinstance(v, str | date))


def sizes(a: Mapping[str, Any]) -> Mapping[str, int]:
    """Lengths of free text inputs: the audit's only trace of them."""
    out = {"qlen": len(a["query"])} if isinstance(a.get("query"), str) else {}
    return out | ({"text_len": utf8_len(a["text"])} if isinstance(a.get("text"), str) else {})


def trace(name: str, raw: Any) -> tuple[tuple[str, ...], Mapping[str, int]]:
    """What the audit may know of a call's arguments before it runs: validated ids and text lengths."""
    try:
        a = tool_args.parse(name, raw)
    except tool_args.BadArg:
        return (), MappingProxyType({})
    return audit_ids(a), sizes(a)


def _run(ctx: Ctx, name: str, a: Mapping[str, Any]) -> Outcome:
    body, n = HANDLERS[name](ctx, a)
    if json_size(body) > OUTPUT_MAX:
        raise ToolError("E_TOO_LARGE")
    return Outcome(body=body, n=n, ids=audit_ids(a), sizes=sizes(a))


def call(ctx: Ctx, name: str, raw: Any) -> Outcome:
    """Run one tool. The name must already be known and visible; unexpected exceptions propagate."""
    try:
        a = tool_args.parse(name, raw)
    except tool_args.BadArg as exc:
        return Outcome(error=ToolError("E_BAD_ARG", exc.field))
    try:
        return _run(ctx, name, a)
    except ToolError as exc:
        return Outcome(error=exc, ids=audit_ids(a), sizes=sizes(a))
    except BadId as exc:
        return Outcome(error=ToolError("E_BAD_ARG", exc.field), ids=audit_ids(a), sizes=sizes(a))
    except ArchiveError as exc:
        code = exc.code if exc.code in MESSAGES else "E_UNAVAILABLE"
        return Outcome(error=ToolError(code), ids=audit_ids(a), sizes=sizes(a))
