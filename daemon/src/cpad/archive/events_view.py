"""What only events.jsonl knows (docs/15 §2.1, §3.4): captures, the open queue, hidden recordings. Pure.

- captures: typed notes and queue items from the device (`item.received`), and memo text from finished
  recordings (`rec.result` with memo_text). Time is the device wall clock when known, otherwise when cpad
  received the item (like the daily note); for a memo, the session start. Shown in local time. A memo's project
  is its effective one (docs/23 §2.1) when `projects` has it, else the one its rec.result recorded;
- the queue is cpad.queue.fold, the same fold `cpa queue ls` uses; an item made from a recording (its from_item,
  e.g. one `cpa filing hand` queued) can be left out with that recording when it is hidden (docs/23 §3.5);
- `rec.hidden{rec}` / `rec.unhidden{rec}` (v0.4.1, docs/13 §11.1), folded in order.
Malformed or unknown records are skipped; unknown fields are ignored.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from datetime import UTC, date, datetime, tzinfo
from typing import Any

from cpad.archive import ids
from cpad.protocol.validate import is_int
from cpad.queue.fold import QueueItem, fold_queue, open_items

Record = Mapping[str, Any]
KINDS = ("note", "queue", "memo")


@dataclass(frozen=True, slots=True)
class Capture:
    id: str
    kind: str  # note | queue | memo
    at: str  # local time, ISO 8601 with offset
    project: str | None
    dest: str | None  # a note's destination (v0.4.1), a memo's intent
    text: str

    @property
    def key(self) -> str:
        """Newest-first sort and page key: epoch milliseconds, then the id."""
        ms = int(datetime.fromisoformat(self.at).timestamp() * 1000)
        return f"{ms:015d}/{self.id}"


def _from_ms(ms: Any, tz: tzinfo | None) -> datetime | None:
    if not is_int(ms):
        return None
    try:
        return datetime.fromtimestamp(ms / 1000, UTC).astimezone(tz)
    except (OverflowError, OSError, ValueError):
        return None


def _from_iso(text: Any, tz: tzinfo | None) -> datetime | None:
    try:
        return datetime.fromisoformat(text).astimezone(tz)
    except (TypeError, ValueError, OverflowError):
        return None


def _str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def _item(e: Record, tz: tzinfo | None) -> tuple[Capture, datetime] | None:
    body, at_dev = e.get("body"), e.get("at_dev")
    if e.get("kind") not in ("note", "queue") or not isinstance(body, Mapping) or not isinstance(e.get("id"), str):
        return None
    wall = at_dev.get("wall_ms") if isinstance(at_dev, Mapping) else None
    when = _from_ms(wall, tz) or _from_iso(e.get("received_at"), tz)
    if when is None or not isinstance(body.get("text"), str):
        return None
    project = None if e.get("orphan") else _str(body.get("project"))
    dest = _str(body.get("dest")) if e["kind"] == "note" else None
    capture = Capture(e["id"], e["kind"], when.isoformat(timespec="seconds"), project, dest, body["text"])
    return capture, when


def _memo(e: Record, tz: tzinfo | None, projects: Mapping[str, str | None]) -> tuple[Capture, datetime] | None:
    if e.get("state") != "done" or e.get("mode") != "memo" or not isinstance(e.get("memo_text"), str):
        return None
    when = _from_ms(e.get("started_ms"), tz)
    if when is None or not isinstance(e.get("rec"), str):
        return None
    project = projects[e["rec"]] if e["rec"] in projects else _str(e.get("project"))
    capture = Capture(e["rec"], "memo", when.isoformat(timespec="seconds"), project, _str(e.get("memo_intent")),
                      e["memo_text"])  # fmt: skip
    return capture, when


def _capture(e: Record, tz: tzinfo | None, projects: Mapping[str, str | None]) -> tuple[Capture, datetime] | None:
    if e.get("ev") == "item.received":
        return _item(e, tz)
    return _memo(e, tz, projects) if e.get("ev") == "rec.result" else None


def _wanted(found: tuple[Capture, datetime], start: date | None, end: date | None, kind: str | None,
            project: str | None) -> bool:  # fmt: skip
    capture, when = found
    in_range = ids.in_range(when.date().isoformat(), start, end)
    return in_range and kind in (None, capture.kind) and project in (None, capture.project)


def captures(events: Iterable[Record], start: date | None = None, end: date | None = None, kind: str | None = None,
             project: str | None = None, tz: tzinfo | None = None,
             projects: Mapping[str, str | None] | None = None) -> tuple[Capture, ...]:  # fmt: skip
    """Captures in range (local dates), newest first; `projects`: session id -> effective project."""
    found = (c for c in (_capture(e, tz, projects or {}) for e in events if isinstance(e, Mapping)) if c is not None)
    chosen = (capture for capture, when in found if _wanted((capture, when), start, end, kind, project))
    return tuple(sorted(chosen, key=lambda c: c.key, reverse=True))


def _sid(value: Any) -> str | None:
    try:
        return ids.parse_sid(value)
    except ids.BadId:
        return None


def _sources(events: Iterable[Record]) -> dict[str, str | None]:
    """qid -> the lower-case session id its first queue.added was made from (from_item), as the fold keeps the first."""
    added = [e for e in events if e.get("ev") == "queue.added" and isinstance(e.get("qid"), str)]
    return {e["qid"]: _sid(e.get("from_item")) for e in reversed(added)}


def queue_rows(events: Iterable[Record], project: str | None = None,
               hidden: frozenset[str] = frozenset()) -> tuple[QueueItem, ...]:  # fmt: skip
    """Open queue items, newest first (the fold `cpa queue ls` uses), but those made from a hidden recording."""
    found = tuple(events)
    sources = _sources(found) if hidden else {}
    return tuple(i for i in open_items(fold_queue(found), project) if sources.get(i.qid) not in hidden)


def hidden(events: Iterable[Record]) -> frozenset[str]:
    """Lower-case session ids hidden by rec.hidden and not shown again by a later rec.unhidden."""
    out: frozenset[str] = frozenset()
    for e in events:
        sid = _sid(e.get("rec")) if e.get("ev") in ("rec.hidden", "rec.unhidden") else None
        if sid is not None:
            out = out | {sid} if e["ev"] == "rec.hidden" else out - {sid}
    return out
