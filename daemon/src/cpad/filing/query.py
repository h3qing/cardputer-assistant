"""The filters and pages of the filing list (docs/23 §3.1): `cpa filing ls` today, MCP list_unfiled (P1-E) next,
so both answer the same question the same way. Pure.

parse() turns raw arguments (argv strings, or JSON values) into a Query, or raises FilingError naming the
argument. A filter reads only the row (filing.rows) and the recording directory's day, never a file:
- filing: new (default) | filed | handed | done | hidden | all, where all is every state but hidden;
- the other-Mac group (a new row with `asked`) only with orphans;
- from / to (the directory's day, both ends included), device (the short id), link (here | unknown; a row
  without a stamp is unknown), mode, project (any of several keys of this Mac) or none, tx (any of several);
- text: a literal, case-insensitive substring (as MCP search), matched by the caller on the title, the memo text
  and the transcript, because only the I/O shell reads the transcript.
Rows come newest first (start time, then id); the cursor is the position of the last row of a page, so a row
filed meanwhile shifts nothing. counts are over every row, whatever the filters.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from datetime import date
from itertools import islice
from typing import Any

from cpad.archive import ids
from cpad.filing.args import E_BAD_ARG, E_UNKNOWN_PROJECT, FilingError, parse_choice, parse_key
from cpad.filing.fold import FILINGS, Known
from cpad.protocol.validate import is_int

FILTERS = (*FILINGS, "all")
LINKS = ("here", "unknown")
MODES = ("memo", "meeting")
TXS = ("pulling", "waiting", "processing", "blocked", "transcript", "done", "failed", "stale")
TEXT_MAX = 200  # as MCP search's query
LIMIT_DEFAULT = 200
LIMIT_MAX = 1000
DEVICE = re.compile(r"[0-9a-f]{12}\Z")
POSITION = re.compile(r"([0-9]{1,16})/([0-9A-Z-]{18})\Z")
CURSOR_KIND = "filing"


@dataclass(frozen=True, slots=True)
class Query:
    filing: str = "new"
    orphans: bool = False
    start: date | None = None
    end: date | None = None
    device: str | None = None
    link: str | None = None
    mode: str | None = None
    projects: tuple[str, ...] = ()
    none: bool = False
    tx: tuple[str, ...] = ()
    text: str | None = None
    limit: int = LIMIT_DEFAULT
    after: tuple[int, str] | None = None  # the cursor: (start, id) of the last row of the previous page
    no_text: bool = False


@dataclass(frozen=True, slots=True)
class Entry:
    """A light row (filing.rows without any file read) and its recording directory's day."""

    row: Mapping[str, Any]
    day: str | None


# --- arguments ------------------------------------------------------------------------------------


def _date(text: Any, field: str) -> date | None:
    if text is None:
        return None
    try:
        return ids.parse_date(text)
    except ids.BadId:
        raise FilingError(E_BAD_ARG, field) from None


def _optional(raw: Mapping[str, Any], name: str, check: Callable[[Any], Any]) -> Any:
    return None if raw.get(name) is None else check(raw[name])


def _device(text: Any) -> str:
    if not isinstance(text, str) or not DEVICE.match(text):
        raise FilingError(E_BAD_ARG, "device")
    return text


def _projects(raw: Mapping[str, Any], known: Known) -> tuple[str, ...]:
    keys = tuple(parse_key(k) for k in raw.get("project") or ())
    if keys and raw.get("none"):
        raise FilingError(E_BAD_ARG, "project")
    if any(k not in known.keys for k in keys):
        raise FilingError(E_UNKNOWN_PROJECT, "project")
    return keys


def _text(text: Any) -> str:
    if not isinstance(text, str) or not 0 < len(text) <= TEXT_MAX:
        raise FilingError(E_BAD_ARG, "text")
    return text


def _limit(value: Any) -> int:
    number = int(value) if isinstance(value, str) and value.isascii() and value.isdigit() else value
    if not is_int(number) or not 1 <= number <= LIMIT_MAX:
        raise FilingError(E_BAD_ARG, "limit")
    return number


def _after(text: Any) -> tuple[int, str]:
    try:
        found = POSITION.match(ids.decode_cursor(CURSOR_KIND, text))
    except ids.BadId:
        found = None
    if found is None:
        raise FilingError(E_BAD_ARG, "cursor")
    return int(found.group(1)), found.group(2)


def _range(raw: Mapping[str, Any]) -> tuple[date | None, date | None]:
    start, end = _date(raw.get("from"), "from"), _date(raw.get("to"), "to")
    if start is not None and end is not None and end < start:
        raise FilingError(E_BAD_ARG, "to")
    return start, end


def parse(raw: Mapping[str, Any], known: Known) -> Query:
    """raw: filing, orphans, from, to, device, link, mode, project (keys), none, tx (words), text, limit, cursor,
    no_text; absent or None means no filter."""
    start, end = _range(raw)
    return Query(
        filing=_optional(raw, "filing", lambda v: parse_choice(v, FILTERS, "filing")) or "new",
        orphans=bool(raw.get("orphans")), start=start, end=end, device=_optional(raw, "device", _device),
        link=_optional(raw, "link", lambda v: parse_choice(v, LINKS, "link")),
        mode=_optional(raw, "mode", lambda v: parse_choice(v, MODES, "mode")), projects=_projects(raw, known),
        none=bool(raw.get("none")), tx=tuple(parse_choice(t, TXS, "tx") for t in raw.get("tx") or ()),
        text=_optional(raw, "text", _text), limit=_optional(raw, "limit", _limit) or LIMIT_DEFAULT,
        after=_optional(raw, "cursor", _after), no_text=bool(raw.get("no_text")),
    )  # fmt: skip


# --- filters --------------------------------------------------------------------------------------


def _filing_ok(row: Mapping[str, Any], q: Query) -> bool:
    if row.get("asked") is not None and not q.orphans:
        return False
    return row["filing"] != "hidden" if q.filing == "all" else row["filing"] == q.filing


def _link(row: Mapping[str, Any]) -> str:
    env = row.get("env")
    return (env.get("link") if isinstance(env, Mapping) else None) or "unknown"


def _project_ok(row: Mapping[str, Any], q: Query) -> bool:
    if q.none:
        return row.get("project") is None
    return not q.projects or row.get("project") in q.projects


def _day_ok(day: str | None, q: Query) -> bool:
    if q.start is None and q.end is None:
        return True
    return day is not None and ids.in_range(day, q.start, q.end)


def keep(e: Entry, q: Query) -> bool:
    """Every filter but text."""
    row = e.row
    return (_filing_ok(row, q) and _day_ok(e.day, q) and (q.device is None or row.get("device") == q.device)
            and (q.link is None or _link(row) == q.link) and (q.mode is None or row.get("kind") == q.mode)
            and _project_ok(row, q) and (not q.tx or row.get("tx") in q.tx))  # fmt: skip


def position(row: Mapping[str, Any]) -> tuple[int, str]:
    started = row["at"].get("wall_ms")
    return (started if is_int(started) and started >= 0 else 0), row["id"]


def _newest_first(pos: tuple[int, str]) -> tuple[int, str]:
    return -pos[0], pos[1]


def candidates(entries: Iterable[Entry], q: Query) -> tuple[Entry, ...]:
    """The entries every filter but text keeps, newest first, after the cursor."""
    kept = sorted((e for e in entries if keep(e, q)), key=lambda e: _newest_first(position(e.row)))
    if q.after is None:
        return tuple(kept)
    return tuple(e for e in kept if _newest_first(position(e.row)) > _newest_first(q.after))


def contains(texts: Iterable[str | None], needle: str) -> bool:
    folded = needle.casefold()
    return bool(folded) and any(folded in t.casefold() for t in texts if isinstance(t, str))


def cursor_after(row: Mapping[str, Any]) -> str:
    """The cursor of the page that starts after `row` (MCP list_unfiled also cuts a page to its output budget)."""
    start, rec = position(row)
    return ids.encode_cursor(CURSOR_KIND, f"{start}/{rec}")


def page(found: Sequence[Entry], q: Query, matches: Callable[[Entry], bool]) -> tuple[tuple[Entry, ...], str | None]:
    """At most q.limit entries that match, and the cursor of the next page when one more matches."""
    taken = tuple(islice((e for e in found if matches(e)), q.limit + 1))
    if len(taken) <= q.limit:
        return taken, None
    return taken[: q.limit], cursor_after(taken[q.limit - 1].row)


def counts(rows: Iterable[Mapping[str, Any]], on_device: int | None) -> dict[str, int | None]:
    new = [row for row in rows if row["filing"] == "new"]
    other = sum(1 for row in new if row.get("asked") is not None)
    return {"new": len(new) - other, "other_mac": other, "clips_waiting": 0, "on_device": on_device}


def shown(row: Mapping[str, Any], q: Query) -> dict[str, Any]:
    return {**row, "title": None, "text": None} if q.no_text else dict(row)
