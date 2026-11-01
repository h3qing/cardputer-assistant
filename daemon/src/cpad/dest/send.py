"""Sending from this Mac (docs/25 §5.4, D14, D21): cpa filing send, cpa dest send / drop / pending / usual. I/O shell.

A send is a person on this Mac choosing, just now, one capture and one destination (origin "mac", how "pick"):
- the text is never in argv: a recording's memo text is read from events.jsonl (waiting.memo_capture), a held
  typed note's from its item.received; meetings, the daily note and the queue are refused (hand is cpa filing hand);
- decide() runs first and a refusal writes nothing (waiting.refusal): a destination that may act (fl k: a url not
  verified as a draft on this Mac) answers E_NEEDS_DEVICE, because only the device's full-text confirmation counts
  for it; a Mac click counts only for drafts, the clipboard and local files;
- a capture already handed or delivered to that destination is refused with E_ALREADY unless --again: a second
  ⌥X or ⌘↩ never pre-fills it twice by accident;
- then, under the events lock and decided again with whatever was appended meanwhile, one dest.decided that starts
  the delivery (refused with E_BUSY while another send of it runs, E_UNKNOWN_ID when another process sent or dropped
  the held row meanwhile), the delivery itself (adapters, the pinned app, the draft occupancy other processes
  recorded), and its dest.handed / delivered / failed. A delivery that raises fails with E_INTERRUPTED; one cut off
  (Ctrl-C, SIGTERM from the Mac app's timeout) writes dest.failed{E_INTERRUPTED} before the process ends; one that
  dies harder is listed as failed after STALE_RUNNING_S (pending.settled). At most once: nothing here retries or
  replays; a failure stays in the list, and only another send by a person tries again.
drop writes dest.dropped for a held or suggested row (the daily-note line stays). usual sets a project's usual
destination in destinations.json, which only preselects it in the list (never sends by itself).
"""

from __future__ import annotations

import re
import signal
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from contextlib import contextmanager
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any

from cpad.config.settings import Paths
from cpad.dest import cli_presets
from cpad.dest.adapters import execute, make_adapters
from cpad.dest.catalog import Catalog, find, state_of, usual_of
from cpad.dest.config import dests_path, load_catalog
from cpad.dest.model import DEST_ID, By, Capture, Delivery, DeliveryResult, Dest, flags, kind_of
from cpad.dest.pending import PendingItem, fold_pending, settled
from cpad.dest.pending import find as find_item
from cpad.dest.policy import decide
from cpad.dest.records import DEST_EVENTS, dest_dropped, dest_failed
from cpad.dest.router import on_mac, on_result
from cpad.dest.waiting import (
    E_ALREADY,
    E_BUSY,
    E_UNKNOWN_ID,
    SendError,
    Waiting,
    memo_capture,
    recent_drafts,
    refusal,
    waiting,
)
from cpad.events.log import EventLog
from cpad.events.records import iso
from cpad.filing import fold, view
from cpad.filing.args import E_BAD_ARG, E_UNAVAILABLE, VERSION, FilingError, parse_id
from cpad.filing.text import display_safe
from cpad.recordings import index

NOW: Callable[[], float] = time.time  # tests pin it
REF_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
DEST_RE = re.compile(DEST_ID[:-1] + r"\Z")
NOT_FROM_MAC = frozenset({"daily", "todo", "queue", "session"})  # the line is written; hand is cpa filing hand
E_INTERRUPTED = "E_INTERRUPTED"
Events = Sequence[Mapping[str, Any]]


@dataclass(frozen=True, slots=True)
class Desk:
    paths: Paths
    log: EventLog
    events: tuple[dict, ...]
    cat: Catalog
    known: fold.Known


def open_desk(paths: Paths) -> Desk:
    cat = load_catalog(dests_path(paths))
    if cat.problem:
        raise SendError(E_UNAVAILABLE, "dests")
    log = EventLog(paths.events)
    return Desk(paths, log, log.load(), cat, view.known(paths.projects))


def parse_ref(text: str, field: str) -> str:
    """A recording id (MonoId, returned upper case) or another capture's id."""
    try:
        return parse_id(text)
    except FilingError:
        if isinstance(text, str) and REF_RE.match(text):
            return text
        raise SendError(E_BAD_ARG, field) from None


def _held(events: Events, ref: str) -> PendingItem | None:
    item = find_item(settled(fold_pending(events), NOW()), ref)
    if item is not None and item.st == "running":
        raise SendError(E_BUSY, "ref")
    return item


def _capture(desk: Desk, ref: str, dest_id: str | None, field: str) -> tuple[Capture, bool]:
    """(the capture, whether it is a held row)."""
    item = _held(desk.events, ref)
    if ref in index.fold(desk.events).archived:
        cap = memo_capture(desk.events, desk.known, ref, "")
    elif item is not None and item.capture.text:
        cap = replace(item.capture, how="pick")
    else:
        raise SendError(E_UNKNOWN_ID, field)
    chosen = dest_id or (item.dest if item else None) or usual_of(desk.cat, cap.project)
    if chosen is None or not DEST_RE.match(chosen) or kind_of(chosen) in NOT_FROM_MAC:
        raise SendError(E_BAD_ARG, "dest")
    return replace(cap, dest=chosen), item is not None


def _already(events: Events, ref: str, dest: str) -> bool:
    """The newest dest.* record of ref at dest says it was handed or delivered."""
    mine = (e.get("ev") for e in reversed(events) if e.get("item") == ref and e.get("dest") == dest)
    return next((ev for ev in mine if ev in DEST_EVENTS), None) in ("dest.handed", "dest.delivered")


@dataclass(frozen=True, slots=True)
class _Ask:
    cap: Capture
    held: bool  # a held row when the desk was read: gone under the lock means another process sent or dropped it
    by: By
    again: bool
    field: str


def _check(events: Events, ask: _Ask) -> PendingItem | None:
    """The held item under the lock; raises SendError for a row gone meanwhile or a capture already there."""
    item = _held(events, ask.cap.ref)
    if ask.held and item is None:
        raise SendError(E_UNKNOWN_ID, ask.field)
    if not ask.again and _already(events, ask.cap.ref, ask.cap.dest):
        raise SendError(E_ALREADY, "dest")
    return item


def _claim(desk: Desk, ask: _Ask) -> tuple[Delivery, tuple[dict, ...]]:
    """Writes the dest.decided that starts the delivery, decided again under the lock; (delivery, others' events)."""
    at, made = iso(NOW()), []

    def prepare(others: tuple[dict, ...]) -> tuple[dict, ...]:
        item = _check((*desk.events, *others), ask)
        step = on_mac(desk.cat, ask.cap, at=at, attempt=(item.attempt if item else 0) + 1, by=ask.by)
        made.append(step)
        return step.records

    others, _ = desk.log.append((), prepare=prepare)
    (delivery,) = made[-1].deliveries
    return delivery, others


def _end(_signum: int, _frame: object) -> None:
    raise SystemExit(128 + signal.SIGTERM)


@contextmanager
def _terminable() -> Iterator[None]:
    """While a delivery runs, SIGTERM (the Mac app's timeout) raises SystemExit, so its dest.failed is written."""
    try:
        before = signal.signal(signal.SIGTERM, _end)
    except ValueError:  # not the main thread: nothing to install
        before = None
    try:
        yield
    finally:
        if before is not None:
            signal.signal(signal.SIGTERM, before)


def _delivered(desk: Desk, d: Delivery, events: Events) -> DeliveryResult:
    """The delivery's result; an error in it is a failure (E_INTERRUPTED); a cut-off writes dest.failed, re-raised."""
    try:
        with _terminable():
            return _run(desk, d, events)
    except Exception:  # noqa: BLE001 (any error in the adapters or the frameworks: the item must not stay running)
        return DeliveryResult(False, E_INTERRUPTED)
    except BaseException:
        desk.log.append((dest_failed(d, code=E_INTERRUPTED, retryable=False, at=iso(NOW())),))
        raise


def _run(desk: Desk, d: Delivery, events: Events) -> DeliveryResult:
    clip = find(desk.cat, "clip")
    adapters = make_adapters(Path.home(), denied=(desk.paths.home,), mac=cli_presets.MAC(), writer=cli_presets.WRITER(),
                             clip_on=lambda: clip is not None and state_of(clip) == "on", clock=NOW,
                             recent=recent_drafts(events, NOW()))  # fmt: skip
    return execute(d, adapters)


def _sent(d: Delivery, result: DeliveryResult) -> dict[str, Any]:
    outcome = "failed" if not result.ok else "handed" if result.via else "delivered"
    why = result.ref if result.via == "clip" and result.ref in ("too_long", "draft_busy") else None
    return {"ok": True, "v": VERSION, "ref": d.capture.ref, "dest": d.dest.id, "label": display_safe(d.dest.label),
            "kind": d.dest.kind, "outcome": outcome, "via": result.via, "why": why,
            "code": None if result.ok else result.code, "leaves_mac": d.dest.leaves_mac}  # fmt: skip


def send(paths: Paths, ref_text: str, dest_id: str | None, *, by: By = "cli", field: str = "ref",
         again: bool = False) -> dict[str, Any]:  # fmt: skip
    """Send one capture from this Mac; the `sent` document. Raises SendError, having written nothing.
    again: send a capture already handed or delivered to that destination once more."""
    desk = open_desk(paths)
    cap, held = _capture(desk, parse_ref(ref_text, field), dest_id, field)
    found = refusal(decide(cap, find(desk.cat, cap.dest), False, "mac"))
    if found is not None:
        raise found
    ask = _Ask(cap, held, by, again, field)
    _check(desk.events, ask)
    delivery, others = _claim(desk, ask)
    result = _delivered(desk, delivery, (*desk.events, *others))
    desk.log.append(on_result(delivery, result, at=iso(NOW())).records)
    return _sent(delivery, result)


def _droppable(desk: Desk, events: Events, ref: str) -> tuple[str, str | None]:
    """(dest, prof) of a held or suggested row; raises SendError."""
    item = _held(events, ref)
    if item is not None:
        return item.dest, item.capture.prof
    row = next((w for w in waiting(events, desk.cat, desk.known, NOW()) if w.ref == ref), None)
    if row is None:
        raise SendError(E_UNKNOWN_ID, "ref")
    prof = index.fold(events).archived.get(ref, {}).get("prof")
    return row.dest, prof if isinstance(prof, str) else None


def drop(paths: Paths, ref_text: str, *, by: By = "cli") -> dict[str, Any]:
    """Not to be sent: dest.dropped for a held or suggested row; the `dropped` document."""
    desk, ref, at = open_desk(paths), parse_ref(ref_text, "ref"), iso(NOW())
    made: list[str] = []

    def prepare(others: tuple[dict, ...]) -> tuple[dict, ...]:
        dest, prof = _droppable(desk, (*desk.events, *others), ref)
        made.append(dest)
        return (dest_dropped(ref, dest, by=by, at=at, prof=prof),)

    desk.log.append((), prepare=prepare)
    return {"ok": True, "v": VERSION, "ref": ref, "dest": made[-1]}


def _row(w: Waiting, cat: Catalog) -> dict[str, Any]:
    dest: Dest | None = find(cat, w.dest)
    return {"ref": w.ref, "why": w.why, "dest": w.dest, "label": display_safe(dest.label) if dest else w.dest,
            "kind": kind_of(w.dest), "fl": flags(dest) if dest else None, "st": w.st, "code": w.code,
            "project": w.project, "src": w.src, "at": w.at, "text": w.text}  # fmt: skip


def pending(paths: Paths, *, expired: bool = False) -> dict[str, Any]:
    """The `pending` document: the rows waiting to be sent (waiting.waiting)."""
    desk = open_desk(paths)
    rows = waiting(desk.events, desk.cat, desk.known, NOW(), expired=expired)
    return {"ok": True, "v": VERSION, "rows": [_row(w, desk.cat) for w in rows]}
