"""The router (docs/14 §6, §7.4): turns a decision into records to append, deliveries to run, a reply to send.

Pure. Every Step is applied in order by the caller (the hub, from v0.5): append `records` in one batch
first (a typed note: together with its item.received, Append before Send; a memo: on their own, because
the recordings service appended the rec.result already), then run `deliveries` one at a time through
adapters.execute(), feeding each DeliveryResult back to on_result(). `reply` answers a dest.send now;
None means later (when its delivery finishes) or not a request.

Hub wiring (D5-I, after v0.4.1 merges; nothing here is called by the daemon yet):
  app start-up:  load_catalog() into the state, then the one Appended(history) (never a second fold of it),
                 then resume(startup=True)
  hub/reduce.py OutboxItem(note): capture_from_item(received) -> origin_of(...) -> route(cap, RouteContext)
                                  Append((received, *step.records)), Send(stored), WriteNote, RunDelivery per delivery
  hub/reduce.py Appended:         state.pending, step = on_appended(...); Append(step.records) first, RunDelivery each.
                                  Memos are routed only from the folded state (undecided_memos), never from the batch.
  hub/reduce.py LinkReady:        resume(startup=False)
  hub/runner.py RunDelivery:      asyncio.to_thread(adapters.execute, d, adapters) -> DeliveryFinished -> on_result()
  device dest.send (D5-B):        on_send()
  housekeeping each minute:       expire()
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass, replace
from datetime import datetime
from typing import Any, Protocol

from cpad.agents.claude_code.argv import check_text
from cpad.dest import records as r
from cpad.dest.catalog import Catalog, state_of
from cpad.dest.catalog import find as find_dest
from cpad.dest.handoff import handed_text
from cpad.dest.model import (
    ADAPTER_KINDS,
    DAILY,
    HOLD_TTL_S,
    MAX_ATTEMPTS,
    By,
    Capture,
    Decision,
    Delivery,
    DeliveryResult,
    Dest,
    Origin,
    delivery_key,
    flags,
    kind_of,
)
from cpad.dest.pending import PendingItem, PendingState, fold_pending, visible
from cpad.dest.pending import find as find_item
from cpad.dest.policy import decide
from cpad.events.records import queue_added
from cpad.hub.snapshots import truncate_utf8
from cpad.notes.render import clean
from cpad.protocol.limits import LIMITS


class SendRequest(Protocol):
    """A parsed dest.send (docs/14 §7.4): go delivers a pending item (to `dest` when set), drop forgets it."""

    @property
    def id(self) -> str: ...
    @property
    def ref(self) -> str: ...
    @property
    def op(self) -> str: ...
    @property
    def dest(self) -> str | None: ...


@dataclass(frozen=True, slots=True)
class Reply:
    code: str | None = None  # None = ack
    field: str | None = None


@dataclass(frozen=True, slots=True)
class Step:
    records: tuple[dict[str, Any], ...] = ()  # to append (one batch, before anything else)
    deliveries: tuple[Delivery, ...] = ()  # to run after the append
    reply: Reply | None = None  # dest.send answer now; None = later or not a request


@dataclass(frozen=True, slots=True)
class RouteContext:
    """What the hub knows when a capture arrives."""

    catalog: Catalog
    online: bool  # the device link is Ready now
    origin: Origin  # policy.origin_of() for typed notes; "queued" for voice memos
    at: str  # daemon ISO time (events.records.iso)


RouteDecision = Step
# Kinds a pending item cannot be re-routed to yet: its daily-note line is already written as a plain note, and
# turning it into a checkbox needs a note write the hub does not have (NotesWriter writes only item.received).
NO_REROUTE: frozenset[str] = frozenset({"todo"})


def queue_text(text: str | None) -> str | None:
    """The queue.added text: control characters as spaces, trimmed, cut to text_max UTF-8 bytes on a character
    boundary (a memo is up to 2000 characters). None when even that could never be spawned (check_text)."""
    cut = truncate_utf8(clean(text or "").strip(), LIMITS["text_max"]).strip()
    return cut if check_text(cut) is None else None


def _decision(cat: Catalog, cap: Capture, online: bool, origin: Origin) -> tuple[Dest | None, Decision, str | None]:
    """(dest, decision, queue text); a queue item whose text could never be spawned is downgraded (bad_text)."""
    dest = find_dest(cat, cap.dest)
    decision = decide(cap, dest, online, origin)
    if decision.action != "queue":
        return dest, decision, None
    text = queue_text(cap.text)
    return (dest, decision, text) if text is not None else (dest, Decision("downgrade", DAILY, "bad_text"), None)


def _decided(cat: Catalog, cap: Capture, *, online: bool, origin: Origin, at: str, attempt: int = 1,
             by: By = "auto", re: str | None = None) -> Step:  # fmt: skip
    dest, decision, text = _decision(cat, cap, online, origin)
    runs = decision.action == "deliver" and dest is not None and dest.kind in ADAPTER_KINDS
    deliveries = (Delivery(delivery_key(cap.ref, dest.id), cap, dest, attempt, by, re),) if runs and dest else ()
    records = [r.dest_decided(cap, decision, origin, at, attempt if deliveries else None)]
    if decision.intent and cap.project and text:
        added = queue_added(qid=None, project=cap.project, text=text, intent=decision.intent, origin="dest", at=at,
                            from_item=cap.ref)  # fmt: skip
        records.append({**added, "voice": True} if decision.voice else added)
    return Step(tuple(records), deliveries)


def _merged(steps: Iterable[Step]) -> Step:
    """Records and deliveries of several steps, in order (none of them a dest.send answer)."""
    done = tuple(steps)
    return Step(tuple(rec for s in done for rec in s.records), tuple(d for s in done for d in s.deliveries))


def on_capture(cat: Catalog, cap: Capture, *, online: bool, origin: Origin, at: str) -> Step:
    return _decided(cat, cap, online=online, origin=origin, at=at)


def on_mac(cat: Catalog, cap: Capture, *, at: str, attempt: int, by: By) -> Step:
    """A person on this Mac sent it (cpa filing send, cpa dest send; docs/25 §5.4): origin "mac", never replayed."""
    return _decided(cat, cap, online=False, origin="mac", at=at, attempt=attempt, by=by)


def route(cap: Capture, ctx: RouteContext) -> RouteDecision:
    """The hub's entry point for a new capture: on_capture() with the context bundled."""
    return on_capture(ctx.catalog, cap, online=ctx.online, origin=ctx.origin, at=ctx.at)


def undecided_memos(pend: PendingState) -> tuple[Capture, ...]:
    """Voice memos folded from a rec.result with a dest and no dest.decided yet, oldest first."""
    return tuple(cap for cap in pend.captures.values() if cap.source == "voice")


def on_appended(cat: Catalog, pend: PendingState, records: Iterable[Mapping[str, Any]], *, online: bool,
                at: str) -> tuple[PendingState, Step]:  # fmt: skip
    """The hub's Appended hook: fold the batch, then route each voice memo still undecided in the folded state.

    Never routes straight from the batch: at start-up the batch is the whole events.jsonl, and a memo decided
    there long ago must not be routed again. rec.result is appended on its own (recordings service), so a
    crash before its dest.decided leaves the memo undecided, and the next start-up routes it, once.
    The returned state does not include the step's records: they come back through the next Appended.
    """
    folded = fold_pending(records, pend)
    steps = (on_capture(cat, cap, online=online, origin="queued", at=at) for cap in undecided_memos(folded))
    return folded, _merged(steps)


def _reroutable(cat: Catalog, dest_id: str) -> bool:
    """Usable, and not a kind whose line the hub cannot write for an item that is already in the daily note."""
    dest = find_dest(cat, dest_id)
    return dest is not None and state_of(dest) == "on" and dest.kind not in NO_REROUTE


def _answer(step: Step) -> Step:
    """A delivery answers when it finishes; otherwise now: ack, or E_BAD_FIELD dest when go was downgraded."""
    if step.deliveries:
        return step
    downgraded = step.records[0]["action"] == "downgrade"
    return replace(step, reply=Reply("E_BAD_FIELD", "dest") if downgraded else Reply())


def on_send(cat: Catalog, pend: PendingState, req: SendRequest, *, online: bool, at: str) -> Step:
    item = find_item(pend, req.ref)
    if item is None:
        return Step(reply=Reply("E_UNKNOWN_ITEM", "ref"))
    if item.st == "running":
        return Step(reply=Reply("E_BUSY"))
    if req.op == "drop":
        return Step((r.dest_dropped(req.ref, item.dest, by="device", at=at, prof=item.capture.prof),), reply=Reply())
    if req.op != "go" or (req.dest is not None and not _reroutable(cat, req.dest)):
        return Step(reply=Reply("E_BAD_FIELD", "op" if req.op != "go" else "dest"))
    cap = replace(item.capture, dest=req.dest, how="pick") if req.dest else replace(item.capture, dest=item.dest)
    step = _decided(cat, cap, online=online, origin="confirmed", at=at, attempt=item.attempt + 1, by="device",
                    re=req.id)  # fmt: skip
    return _answer(step)


def on_result(d: Delivery, result: DeliveryResult, *, at: str) -> Step:
    """dest.delivered, dest.handed (another app was handed it: via url or clip) or dest.failed."""
    reply = (Reply() if result.ok else Reply("E_INTERNAL")) if d.re else None
    if result.ok and result.via is not None:
        text = handed_text(d.capture) or ""
        return Step((r.dest_handed(d, at=at, via=result.via, text=text, ref=result.ref),), reply=reply)
    if result.ok:
        return Step((r.dest_delivered(d, at=at, ref=result.ref),), reply=reply)
    failed = r.dest_failed(d, code=result.code or "E_INTERNAL", retryable=result.retryable, at=at)
    return Step((failed,), reply=reply)


def _ts(iso: str) -> float | None:
    try:
        return datetime.fromisoformat(iso).timestamp()
    except ValueError:
        return None


def expire(pend: PendingState, *, now: float, at: str) -> Step:
    """dest.expired for every held or failed item older than HOLD_TTL_S (the daily-note line stays)."""
    cutoff = now - HOLD_TTL_S
    old = (i for i in visible(pend) if (_ts(i.since) or cutoff) < cutoff)
    return Step(tuple(r.dest_expired(i.capture.ref, i.dest, at=at, prof=i.capture.prof) for i in old))


def _known(cat: Catalog, item: PendingItem) -> Dest:
    """The destination of a pending item; one removed since is rebuilt from its id (its kind's own flags)."""
    return find_dest(cat, item.dest) or Dest(item.dest, kind_of(item.dest) or "daily", item.dest, 1)


def _resume_one(cat: Catalog, item: PendingItem, at: str, startup: bool) -> Step:
    live = "l" in flags(_known(cat, item))
    by_hand = item.origin == "mac" or item.capture.text is None  # a send from this Mac: only a person sends again
    running = startup and item.st == "running"
    if running and (live or by_hand):
        d = Delivery(delivery_key(item.capture.ref, item.dest), item.capture, _known(cat, item), item.attempt)
        return Step((r.dest_failed(d, code="E_INTERRUPTED", retryable=False, at=at),))
    retry = item.st == "failed" and item.retryable and item.attempt < MAX_ATTEMPTS and not live and not by_hand
    if running or retry:
        cap = replace(item.capture, dest=item.dest)
        return _decided(cat, cap, online=False, origin="queued", at=at, attempt=item.attempt + 1)
    return Step()


def resume(pend: PendingState, cat: Catalog, *, at: str, startup: bool) -> Step:
    """Recovery (§6.2): startup=True once when cpad starts, startup=False on every link-up. No default on purpose.

    Running at start-up means interrupted: a file is decided and delivered again (idempotent by block id),
    a live action fails with E_INTERRUPTED and waits for Enter (a restart never copies by itself). At link-up
    a running delivery is still running and is left alone. Retryable archive failures are retried until
    MAX_ATTEMPTS; then only Enter on the device retries. A send from this Mac (origin "mac", docs/25 §7.4) is never
    decided or delivered again here: running at start-up it fails with E_INTERRUPTED, failed it keeps waiting.
    """
    return _merged(_resume_one(cat, item, at, startup) for item in pend.items)
