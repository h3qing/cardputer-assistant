"""Pending deliveries: a pure fold over events.jsonl (docs/14 §6.2), written like queue/fold.py.

  dest.decided hold                       -> hold     (the device shows it; Enter: go, Opt+D: drop)
  dest.decided deliver, adapter kind      -> running  (Mac only; the record's attempt)
  dest.failed                             -> failed   (the device shows it; Enter retries)
  dest.delivered / handed / dropped / expired, v0.4.1 dest.downgraded, a decision without a delivery -> gone

dest.* records carry no text, but a delivery needs it, so `captures` bridges an item.received /
rec.result to its first dest.decided; the capture then lives in its PendingItem until the final
outcome. A recording sent from this Mac (origin "mac", docs/25 §5.4) has no such record: its decision starts a
skeleton capture without text, so a failure stays visible; whoever sends it again reads the text from rec.result.
Captures never decided (v0.4.1 todo notes, say) are bounded to the newest CAPTURES_MAX.
Idempotent: folding records onto the state they made changes nothing (every update is an overwrite; a
dest.decided that starts a delivery carries its attempt). The hub still folds each record once.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from datetime import datetime
from types import MappingProxyType
from typing import Any, Literal

from cpad.dest.capture import capture_from_item, capture_from_result
from cpad.dest.model import ADAPTER_KINDS, HOWS, Capture

CAPTURES_MAX = 256
STALE_RUNNING_S = 60.0  # a Mac send's cpa process is gone by then (the Mac app gives it 30 s)
FINAL = frozenset({"dest.delivered", "dest.handed", "dest.dropped", "dest.expired", "dest.downgraded"})
Status = Literal["hold", "running", "failed"]


def _no_captures() -> Mapping[str, Capture]:
    return MappingProxyType({})


@dataclass(frozen=True, slots=True)
class PendingItem:
    capture: Capture
    dest: str  # effective dest id
    st: Status
    since: str  # ISO time it entered this state
    attempt: int = 0  # deliveries started so far
    code: str | None = None
    retryable: bool = False
    origin: str | None = None  # the decision's origin: "mac" for a send from this Mac


@dataclass(frozen=True, slots=True)
class PendingState:
    items: tuple[PendingItem, ...] = ()  # oldest first
    captures: Mapping[str, Capture] = field(default_factory=_no_captures)  # received, not yet decided


def find(state: PendingState, ref: str) -> PendingItem | None:
    return next((i for i in state.items if i.capture.ref == ref), None)


def visible(state: PendingState) -> tuple[PendingItem, ...]:
    """What the device lists: hold and failed."""
    return tuple(i for i in state.items if i.st != "running")


def _stale(item: PendingItem, now: float) -> bool:
    try:
        since = datetime.fromisoformat(item.since).timestamp()
    except ValueError:
        return False
    return item.st == "running" and item.origin == "mac" and now - since > STALE_RUNNING_S


def settled(state: PendingState, now: float) -> PendingState:
    """A send from this Mac still running after STALE_RUNNING_S was cut off before it wrote its outcome (the process
    was killed, or crashed): failed with E_INTERRUPTED, so it is listed and a person can send it again or drop it.
    Nothing is written here; the next send or drop writes its own record. The hub's own deliveries are untouched."""
    cut = tuple(replace(i, st="failed", code="E_INTERRUPTED", retryable=False) if _stale(i, now) else i
                for i in state.items)  # fmt: skip
    return replace(state, items=cut)


def _put(state: PendingState, item: PendingItem) -> PendingState:
    ref = item.capture.ref
    found = find(state, ref) is not None
    items = tuple(item if i.capture.ref == ref else i for i in state.items) if found else (*state.items, item)
    return replace(state, items=items, captures=_without(state.captures, ref))


def _forget(state: PendingState, ref: str) -> PendingState:
    items = tuple(i for i in state.items if i.capture.ref != ref)
    return replace(state, items=items, captures=_without(state.captures, ref))


def _without(captures: Mapping[str, Capture], ref: str) -> Mapping[str, Capture]:
    return captures if ref not in captures else MappingProxyType({k: v for k, v in captures.items() if k != ref})


def _remember(state: PendingState, cap: Capture | None) -> PendingState:
    if cap is None or find(state, cap.ref) is not None:
        return state
    kept = {k: v for k, v in state.captures.items() if k != cap.ref}
    newest = list(kept.items())[-(CAPTURES_MAX - 1) :]
    return replace(state, captures=MappingProxyType({**dict(newest), cap.ref: cap}))


def _decided(state: PendingState, e: Mapping[str, Any]) -> PendingState:
    ref = e.get("item")
    if not isinstance(ref, str):
        return state
    existing = find(state, ref)
    cap = existing.capture if existing else state.captures.get(ref) or _skeleton(e, ref)
    if cap is None:
        return state
    action, kind = e.get("action"), e.get("kind")
    if action not in ("hold", "deliver") or (action == "deliver" and kind not in ADAPTER_KINDS):
        return _forget(state, ref)
    dest, want, how = e.get("dest"), e.get("want"), e.get("how")
    if not isinstance(dest, str):
        return state
    cap = replace(cap, dest=want if isinstance(want, str) else dest, how=how if how in HOWS else cap.how)
    st: Status = "hold" if action == "hold" else "running"
    origin = e.get("origin") if isinstance(e.get("origin"), str) else None
    return _put(state, PendingItem(cap, dest, st, str(e.get("at")), _attempt(e, existing), origin=origin))


def _skeleton(e: Mapping[str, Any], ref: str) -> Capture | None:
    """The capture of a Mac send, from its decision alone: no text (it lives in rec.result), never replayed."""
    dest = e.get("want")
    if e.get("origin") != "mac" or not isinstance(dest, str):
        return None
    prof = e.get("prof")
    return Capture(ref, "voice" if e.get("src") == "voice" else "typed", dest, "pick" if e.get("how") == "pick"
                   else "app", prof=prof if isinstance(prof, str) else None)  # fmt: skip


def _attempt(e: Mapping[str, Any], existing: PendingItem | None) -> int:
    """A delivery's own attempt when the record carries it (idempotent); else counted up from the item."""
    given = e.get("attempt")
    if e.get("action") == "deliver" and isinstance(given, int) and not isinstance(given, bool):
        return given
    return (existing.attempt if existing else 0) + (e.get("action") == "deliver")


def _failed(state: PendingState, e: Mapping[str, Any]) -> PendingState:
    existing = find(state, e.get("item")) if isinstance(e.get("item"), str) else None
    if existing is None:
        return state
    attempt = e.get("attempt") if isinstance(e.get("attempt"), int) else existing.attempt
    item = replace(existing, st="failed", since=str(e.get("at")), attempt=attempt, code=e.get("code"),
                   retryable=bool(e.get("retryable")))  # fmt: skip
    return _put(state, item)


def _apply(state: PendingState, e: Mapping[str, Any]) -> PendingState:
    ev = e.get("ev")
    if ev == "item.received":
        return _remember(state, capture_from_item(e))
    if ev == "rec.result":
        return _remember(state, capture_from_result(e))
    if ev == "dest.decided":
        return _decided(state, e)
    if ev == "dest.failed":
        return _failed(state, e)
    if ev in FINAL and isinstance(e.get("item"), str):
        return _forget(state, e["item"])
    return state


def fold_pending(events: Iterable[Mapping[str, Any]], start: PendingState | None = None) -> PendingState:
    state = start or PendingState()
    for event in events:
        state = _apply(state, event)
    return state
