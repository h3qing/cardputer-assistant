"""The queue is a pure fold over events.jsonl (05 §7.7). qid is q<n>, allocated monotonically.

`cpa queue reopen` (docs/20 U12) brings a closed item (queue.done, queue.spawned) back exactly as it was: the
queue.reopened record carries the item, found by one scan of the log (closed_item), so the fold itself keeps
no closed items and stays linear in the log.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, replace
from typing import Any

QID = re.compile(r"q([0-9]{1,10})\Z")
CLOSING = ("queue.done", "queue.spawned")  # queue.done{by:"undo"} withdraws a hand (docs/23 D17): closed alike
OPENING = ("queue.added", "queue.reopened")  # queue.reopened carries the item as queue.added did
INBOX = "inbox"  # origin of an item handed from a recording by `cpa filing hand` (docs/23 §3.4)


@dataclass(frozen=True, slots=True)
class QueueItem:
    qid: str
    project: str
    text: str
    intent: str
    sid: str | None
    origin: str
    uncertain: bool = False

    @property
    def voice(self) -> bool:
        """Spoken, not typed: it starts in plan mode and reads Voice· on the board (docs/23 §3.4, docs/22 §5.4).
        The record's own voice field is dropped here, so this goes by origin until docs/22 keeps that field."""
        return self.origin == INBOX


@dataclass(frozen=True, slots=True)
class QueueState:
    items: tuple[QueueItem, ...] = ()
    last_qid: int = 0


def _qid_number(qid: Any) -> int | None:
    match = QID.match(qid) if isinstance(qid, str) else None
    return int(match.group(1)) if match else None


def _item(event: Mapping[str, Any]) -> QueueItem | None:
    fields = (event.get("project"), event.get("text"), event.get("intent"))
    if not all(isinstance(f, str) for f in fields):
        return None
    return QueueItem(event["qid"], *fields, event.get("sid"), str(event.get("origin")), bool(event.get("uncertain")))


def _apply(state: QueueState, event: Mapping[str, Any]) -> QueueState:
    number = _qid_number(event.get("qid"))
    if number is None:
        return state
    if event.get("ev") in OPENING and find(state, event["qid"]) is None:
        item = _item(event)
        return replace(state, items=(*state.items, item), last_qid=max(state.last_qid, number)) if item else state
    if event.get("ev") in CLOSING:
        return replace(state, items=tuple(i for i in state.items if i.qid != event["qid"]))
    return state


def fold_queue(events: Iterable[Mapping[str, Any]], start: QueueState | None = None) -> QueueState:
    state = start or QueueState()
    for event in events:
        state = _apply(state, event)
    return state


def next_qid(state: QueueState) -> str:
    return f"q{state.last_qid + 1}"


def assign_qids(records: Iterable[Mapping[str, Any]], state: QueueState) -> tuple[dict[str, Any], ...]:
    """Give every queue.added without a qid the next free one (done under the events.jsonl lock)."""
    recs = tuple(records)
    unnumbered = [i for i, r in enumerate(recs) if r.get("ev") == "queue.added" and r.get("qid") is None]
    qids = {index: f"q{state.last_qid + n}" for n, index in enumerate(unnumbered, start=1)}
    return tuple({**r, "qid": qids[i]} if i in qids else dict(r) for i, r in enumerate(recs))


def find(state: QueueState, qid: str) -> QueueItem | None:
    return next((i for i in state.items if i.qid == qid), None)


def closed_item(events: Iterable[Mapping[str, Any]], qid: str) -> QueueItem | None:
    """The item as it was when queue.done or queue.spawned closed it; None while it is open or unknown.

    One scan of the log, as the fold reads it (only `cpa queue reopen` pays for it).
    """
    if _qid_number(qid) is None:
        return None
    item: QueueItem | None = None
    is_open = False
    for event in (e for e in events if e.get("qid") == qid):
        opened = _item(event) if event.get("ev") in OPENING and not is_open else None
        item, is_open = (opened, True) if opened else (item, is_open and event.get("ev") not in CLOSING)
    return None if is_open else item


def open_items(state: QueueState, project: str | None = None) -> tuple[QueueItem, ...]:
    """Newest first (US-08 AC1)."""
    chosen = (i for i in state.items if project is None or i.project == project)
    return tuple(sorted(chosen, key=lambda i: _qid_number(i.qid) or 0, reverse=True))
