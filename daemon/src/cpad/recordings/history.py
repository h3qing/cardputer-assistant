"""The recordings app's history (docs/13 §8.1, §8.2, §12.2). Pure: rows in, rec.items rows and events out.

- page(): sessions.rows() newest first (sessions.newest_first), hidden ones left out, one filter tab;
- the cursor is opaque to the device: 't<start ms, base 36>_<session id without dashes>' or 'u_<id>' for a
  session without a start time, at most 29 characters. The next page holds the rows that sort after it,
  so a session that arrives between two pages never shifts or repeats a row;
- title: the result's title, else the first 24 characters of the memo text, else 'Meeting 42m' ('会议 42m' in
  Chinese: the language when the page is built, cpad.i18n); control characters become spaces and the text is cut
  to rec_title_max UTF-8 bytes on a character boundary;
- todo_n: 1 / 0 for a finished memo (a checkbox line or not, route.is_todo), the action count for a
  summarised meeting (the service counts the unchecked boxes of summary.md instead when it can);
- change(): the events one rec.act appends, none when the session already looks that way.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping
from typing import Any

from cpad.i18n import T
from cpad.notes.render import clean
from cpad.protocol.limits import LIMITS
from cpad.protocol.rec_messages import RecRow
from cpad.recordings import index, records, route, sessions

Row = Mapping[str, Any]
B36 = "0123456789abcdefghijklmnopqrstuvwxyz"
CURSOR = re.compile(r"(?:t([0-9a-z]{1,11})|u)_([0-9A-HJKMNP-TV-Z]{16})")
TIME_MAX = 2**53 - 1
UINT32_MAX = 2**32 - 1
COUNT_MAX = 65535
MEMO_CHARS = 24
FILTERS = {
    "all": lambda row: True,
    "starred": lambda row: bool(row.get("starred")),
    "memo": lambda row: row.get("mode") == "memo",
    "meeting": lambda row: row.get("mode") == "meeting",
}


class BadCursor(ValueError):
    """The device sent a cursor this cpad did not make."""


def _b36(n: int) -> str:
    out = ""
    while True:
        n, digit = divmod(n, 36)
        out = B36[digit] + out
        if not n:
            return out


def cursor_of(row: Row) -> str:
    sid = row["sid"].replace("-", "")
    started = row.get("started_ms")
    return f"u_{sid}" if started is None else f"t{_b36(started)}_{sid}"


def _after(text: str) -> tuple[int, int, str]:
    """The sessions.newest_first key of the row a cursor names."""
    found = CURSOR.fullmatch(text)
    if found is None or (found[1] is not None and int(found[1], 36) > TIME_MAX):
        raise BadCursor(text)
    compact = found[2]
    sid = f"{compact[:7]}-{compact[7:12]}-{compact[12:]}"
    return (0, 0, sid) if found[1] is None else (1, -int(found[1], 36), sid)


def _clamp(value: Any, top: int) -> int | None:
    return None if value is None else max(0, min(int(value), top))


def _clip(text: Any, chars: int | None = None) -> str | None:
    if not isinstance(text, str):
        return None
    head = clean(text).strip()[:chars].encode("utf-8", "replace")[: LIMITS["rec_title_max"]]
    return head.decode("utf-8", "ignore").strip() or None


def title(row: Row, results: Iterable[Row]) -> str | None:
    for e in reversed(tuple(results)):
        found = _clip(e.get("title")) or _clip(e.get("memo_text"), MEMO_CHARS)
        if found:
            return found
    if row.get("mode") != "meeting":
        return None
    minutes = max(1, (int(row.get("dur_ms") or 0) + 30000) // 60000)
    return T(f"会议 {minutes}m", f"Meeting {minutes}m")


def todo_n(mode: Any, done: Row | None) -> int | None:
    if done is None:
        return None
    if mode == "memo":
        return int(route.is_todo(done))
    return _clamp(done.get("actions_n") or 0, COUNT_MAX) if done.get("level") == "summary" else None


def _wall(started: Any) -> int | None:
    return started if isinstance(started, int) and LIMITS["epoch_ms_min"] <= started <= TIME_MAX else None


def rec_row(row: Row, idx: index.RecIndex) -> RecRow:
    results = idx.results.get(row["sid"], ())
    done = next((e for e in reversed(results) if e.get("state") == "done"), None)
    return RecRow(rec=row["sid"], mode=row["mode"], st=row["state"], wall_ms=_wall(row.get("started_ms")),
                  dur_ms=_clamp(row.get("dur_ms"), UINT32_MAX), project=row.get("project"),
                  starred=True if row.get("starred") else None, marks=_clamp(row.get("marks"), COUNT_MAX) or None,
                  title=title(row, results), todo_n=todo_n(row["mode"], done))  # fmt: skip


def page(rows: Iterable[Row], idx: index.RecIndex, *, cursor: str | None, max: int,
         filter: str) -> tuple[tuple[RecRow, ...], str | None]:  # fmt: skip
    """One page of at most `max` rows after `cursor` (None: the first page) and the cursor of the next."""
    after = _after(cursor) if cursor is not None else None
    keep = FILTERS[filter]
    found = sorted((row for row in rows if not row.get("hidden") and keep(row)), key=sessions.newest_first)
    rest = [row for row in found if after is None or sessions.newest_first(row) > after]
    shown = rest[:max]
    nxt = cursor_of(shown[-1]) if len(rest) > len(shown) else None
    return tuple(rec_row(row, idx) for row in shown), nxt


def change(row: Row, op: str, project: str | None, at: str) -> tuple[dict[str, Any], ...]:
    """Events for one rec.act (docs/13 §8.4: hide only hides; there is no delete)."""
    rec = row["sid"]
    if op in ("star", "unstar"):
        starred = op == "star"
        return () if bool(row.get("starred")) == starred else (records.edited_star(rec=rec, starred=starred, at=at),)
    if op == "tag":
        return () if row.get("project") == project else (records.edited_project(rec=rec, project=project, at=at),)
    if op == "hide":
        return () if row.get("hidden") else (records.hidden(rec=rec, at=at),)
    raise ValueError(f"unknown rec.act op {op}")
