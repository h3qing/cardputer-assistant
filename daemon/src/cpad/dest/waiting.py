"""Waiting to be sent (docs/25 §5.4, §3.4): what waits for a send from this Mac, and what a Mac send hands over. Pure.

waiting() lists, oldest first in each group:
- held: what the router or an earlier send left waiting (fold_pending: hold or failed; a send from this Mac
  still running after STALE_RUNNING_S was cut off: failed, E_INTERRUPTED);
- usual: a memo whose project has a usual destination (D21), only a preselection: a memo with memo text, not a
  meeting, not hidden or handed to AI, archived within the last day, with no destination of its own and no dest.*
  record yet, while that destination is on. Ignoring it writes nothing; after a day it is no longer offered;
- expired (only when asked): what dest.expired closed in the last week.
memo_capture() is the capture of a recording sent from this Mac: its memo text only (never the transcript put
together, docs/25 §5.2 check 7), its effective project (filing.fold), `how` pick. refusal() names, as a fixed
code, why decide() would not deliver a Mac send (origin "mac"); nothing is written for a refusal.
recent_drafts() is the draft occupancy (docs/25 §3.6) a new cpa process seeds from dest.handed{via:"url", to}.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from datetime import datetime
from types import MappingProxyType
from typing import Any, Literal

from cpad.dest.capture import capture_from_item, stamp
from cpad.dest.catalog import Catalog, find, state_of, usual_of
from cpad.dest.handlers import DRAFT_HOLD_S
from cpad.dest.model import HOLD_TTL_S, Capture, Decision, Reason
from cpad.dest.pending import PendingItem, fold_pending, settled, visible
from cpad.dest.records import DEST_EVENTS
from cpad.filing import fold, rows
from cpad.filing.args import E_BAD_ARG, E_NO_TEXT, E_UNAVAILABLE, E_UNKNOWN_ID, FilingError, parse_id
from cpad.filing.text import excerpt
from cpad.recordings import index

TEST_PREFIX = "test-"  # the item of a one-time test (cpa dest test ID --send)
E_UNKNOWN_DEST = "E_UNKNOWN_DEST"
E_DEST_OFF = "E_DEST_OFF"
E_OTHER_PROFILE = "E_OTHER_PROFILE"
E_TOO_LONG = "E_TOO_LONG"
E_NEEDS_DEVICE = "E_NEEDS_DEVICE"
E_BUSY = "E_BUSY"
E_ALREADY = "E_ALREADY"
CODES = (E_BAD_ARG, E_UNKNOWN_ID, E_UNKNOWN_DEST, E_DEST_OFF, E_OTHER_PROFILE, E_NO_TEXT, E_TOO_LONG, E_NEEDS_DEVICE,
         E_BUSY, E_ALREADY, E_UNAVAILABLE)  # fmt: skip
USAGE = (E_BAD_ARG, E_UNKNOWN_DEST)
EXPIRED_KEEP_S = 7 * 86_400
TEXT_MAX = rows.TEXT_MAX
Why = Literal["held", "usual", "expired"]
REASONS: Mapping[Reason, tuple[str, str]] = MappingProxyType({
    "meeting": (E_BAD_ARG, "id"), "unknown": (E_UNKNOWN_DEST, "dest"), "unavailable": (E_DEST_OFF, "dest"),
    "disabled": (E_DEST_OFF, "dest"), "no_optin": (E_DEST_OFF, "dest"), "other_profile": (E_OTHER_PROFILE, "dest"),
    "no_text": (E_NO_TEXT, "text"), "too_long": (E_TOO_LONG, "text"), "needs_device": (E_NEEDS_DEVICE, "dest"),
})  # fmt: skip


class SendError(FilingError):
    """A refused Mac send, pending or drop: a fixed code and the argument at fault; nothing was written."""

    @property
    def exit_code(self) -> int:
        return 2 if self.code in USAGE else 1


@dataclass(frozen=True, slots=True)
class Waiting:
    ref: str
    why: Why
    dest: str
    at: str  # when it started waiting: held / failed since, archived, expired
    src: Literal["voice", "typed"]
    project: str | None
    text: str | None  # display_safe, at most TEXT_MAX characters
    st: Literal["hold", "failed"] | None = None
    code: str | None = None


def refusal(decision: Decision) -> SendError | None:
    """Why a Mac send is refused, or None when decide() delivers it."""
    if decision.action == "deliver":
        return None
    code, field = REASONS.get(decision.reason, (E_BAD_ARG, "dest"))
    return SendError(code, field)


def _text(result: Mapping[str, Any]) -> str | None:
    memo = result.get("memo_text")
    return memo if isinstance(memo, str) else None


def _key(project: Any) -> str | None:
    return project if fold.is_key(project) else None


def memo_capture(events: Sequence[Mapping[str, Any]], known: fold.Known, rec_text: str, dest: str) -> Capture:
    """The capture of an archived recording sent to `dest` from this Mac; raises SendError (field id)."""
    try:
        rec = parse_id(rec_text)
    except FilingError:
        raise SendError(E_BAD_ARG, "id") from None
    idx = index.fold(events)
    session = idx.archived.get(rec)
    if session is None:
        raise SendError(E_UNKNOWN_ID, "id")
    project = fold.effective_project(idx, fold.fold(events), rec, known)
    prof, dur = session.get("prof"), session.get("dur_ms")
    when = stamp(session.get("started_ms"), str(session.get("at")))
    return Capture(rec, "voice", dest, "pick", _key(project), _text(rows.done_result(idx, rec)), when,
                   session.get("mode") != "memo", prof if isinstance(prof, str) else None,
                   dur if isinstance(dur, int) and not isinstance(dur, bool) else None)  # fmt: skip


def _time(value: Any) -> float | None:
    try:
        return datetime.fromisoformat(value).timestamp()
    except (TypeError, ValueError):
        return None


def _filled(e: Mapping[str, Any], now: float) -> tuple[tuple[str, str], float] | None:
    """((dest, to), when) of a dest.handed through a link within DRAFT_HOLD_S of now."""
    when = _time(e.get("at")) if e.get("ev") == "dest.handed" and e.get("via") == "url" else None
    dest, to = e.get("dest"), e.get("to")
    if when is None or now - when >= DRAFT_HOLD_S or not isinstance(dest, str) or not isinstance(to, str):
        return None
    return (dest, to), when


def recent_drafts(events: Iterable[Mapping[str, Any]], now: float) -> Mapping[tuple[str, str], float]:
    """(dest, to) -> when the newest link filled that draft, for links handed within DRAFT_HOLD_S of now. A one-time
    test's draft stops counting once its answer is recorded (dest.changed{op:"verify"}): the person was told to
    delete it from the input box, so the first real send is a link again."""
    found: dict[tuple[str, str], float] = {}
    tests: dict[tuple[str, str], float] = {}
    for e in events:
        if e.get("ev") == "dest.changed" and e.get("op") == "verify":
            tests = {k: v for k, v in tests.items() if k[0] != e.get("id")}
            continue
        filled = _filled(e, now)
        if filled is not None:
            into = tests if str(e.get("item", "")).startswith(TEST_PREFIX) else found
            into[filled[0]] = max(filled[1], into.get(filled[0], filled[1]))
    merged = {k: max(v, found.get(k, v)) for k, v in tests.items()}
    return MappingProxyType({**found, **merged})


@dataclass(frozen=True, slots=True)
class _Book:
    idx: index.RecIndex
    log: fold.FilingLog
    known: fold.Known
    touched: frozenset[str]  # refs any dest.* record names
    last: Mapping[str, Mapping[str, Any]]  # ref -> its newest dest.* record


def _book(events: Sequence[Mapping[str, Any]], known: fold.Known) -> _Book:
    dest_events = [e for e in events if e.get("ev") in DEST_EVENTS and isinstance(e.get("item"), str)]
    last = {e["item"]: e for e in dest_events}
    return _Book(index.fold(events), fold.fold(events), known, frozenset(last), MappingProxyType(last))


def _memo_text(book: _Book, rec: str) -> str | None:
    return _text(rows.done_result(book.idx, rec)) if rec in book.idx.archived else None


def _project(book: _Book, rec: str) -> str | None:
    found = fold.effective_project(book.idx, book.log, rec, book.known) if rec in book.idx.archived else None
    return _key(found)


def _held(book: _Book, item: PendingItem) -> Waiting:
    cap = item.capture
    text = cap.text or _memo_text(book, cap.ref)
    return Waiting(cap.ref, "held", item.dest, item.since, cap.source, cap.project or _project(book, cap.ref),
                   excerpt(text, TEXT_MAX), st="failed" if item.st == "failed" else "hold", code=item.code)  # fmt: skip


def _suggested(book: _Book, cat: Catalog, rec: str, now: float) -> Waiting | None:
    session = book.idx.archived[rec]
    f = fold.filing_of(book.idx, book.log, rec, book.known)
    text = _memo_text(book, rec)
    dest = find(cat, usual_of(cat, _key(f.project)) or "")
    since = _time(session.get("at"))
    fresh = since is not None and 0 <= now - since <= HOLD_TTL_S
    usable = dest is not None and state_of(dest) == "on" and fresh and bool(text and text.strip())
    if not usable or session.get("mode") != "memo" or session.get("dest") or f.filing in ("hidden", "handed"):
        return None
    return Waiting(rec, "usual", dest.id, str(session.get("at")), "voice", _key(f.project), excerpt(text, TEXT_MAX))


def _typed_text(events: Sequence[Mapping[str, Any]], ref: str) -> tuple[str | None, str | None]:
    found = next((capture_from_item(e) for e in events if e.get("ev") == "item.received" and e.get("id") == ref), None)
    return (found.text, found.project) if found else (None, None)


def _expired(book: _Book, events: Sequence[Mapping[str, Any]], e: Mapping[str, Any]) -> Waiting:
    ref = e["item"]
    typed, typed_project = _typed_text(events, ref) if ref not in book.idx.archived else (None, None)
    src: Literal["voice", "typed"] = "voice" if ref in book.idx.archived else "typed"
    return Waiting(ref, "expired", str(e.get("dest")), str(e.get("at")), src,
                   typed_project or _project(book, ref), excerpt(typed or _memo_text(book, ref), TEXT_MAX))  # fmt: skip


def _expired_rows(book: _Book, events: Sequence[Mapping[str, Any]], now: float) -> tuple[Waiting, ...]:
    def recent(e: Mapping[str, Any]) -> bool:
        when = _time(e.get("at"))
        return e.get("ev") == "dest.expired" and when is not None and now - when <= EXPIRED_KEEP_S

    return tuple(_expired(book, events, e) for e in book.last.values() if recent(e))


def waiting(events: Sequence[Mapping[str, Any]], cat: Catalog, known: fold.Known, now: float, *,
            expired: bool = False) -> tuple[Waiting, ...]:  # fmt: skip
    """The rows waiting to be sent: held first, then usual, then (when asked) expired."""
    book = _book(events, known)
    held = tuple(_held(book, item) for item in visible(settled(fold_pending(events), now)))
    fresh = (_suggested(book, cat, rec, now) for rec in sorted(book.idx.archived) if rec not in book.touched)
    usual = tuple(sorted((w for w in fresh if w is not None), key=lambda w: w.at))
    return held + usual + (_expired_rows(book, events, now) if expired else ())
