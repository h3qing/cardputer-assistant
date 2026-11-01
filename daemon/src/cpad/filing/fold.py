"""The filing fold (docs/23 §2.1, §2.2): what was decided for each recording, from events.jsonl alone. Pure.

Every reader asks this module, so cpa rec, the recordings app, the daily note, MCP (recordings, todos,
captures) and the export agree on one effective project:
- the last rec.edited{project} or queue item made from the recording (queue.added with from_item = the
  session, a "hand"), in log order; a hand withdrawn by queue.done{by:"undo"} no longer counts (D17), unless the
  device had started it meanwhile (queue.spawned comes after the spawn, so an undo can land while it runs);
  `cpa queue reopen` (queue.reopened, docs/20 U12) opens a closed hand again and makes a withdrawn one count again;
- otherwise what was recorded: a memo's manifest project (rec.archived), a meeting's last landed segment
  (its key, or this Mac's key for its slot), as manifest.json also has it since #50 (#46).
The effective destination, until docs/14 merges: segment 0's dest "todo", unless a dest.downgraded names it.
State, in this order: hidden (rec.hidden) -> handed (a live hand) -> filed (a registered project or a
destination) -> done (rec.edited{done:true}) -> new. A new session whose project is a key this Mac does not
know is an orphan: its own group, carrying the key it asked for (a value that is no key at all is not asked for).
Malformed records are skipped, as recordings.index does. Records are never mutated.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field, replace
from types import MappingProxyType
from typing import Any

from cpad.projects.registry import Project
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import matches
from cpad.recordings import index, records

Record = Mapping[str, Any]
FILINGS = ("new", "filed", "handed", "done", "hidden")
LINES = ("rec.result", "rec.stale", "rec.filed")  # records that put a recording line in the daily note
CLOSING = ("queue.done", "queue.spawned")
REOPENED = "queue.reopened"
TODO = "todo"


def _empty() -> Mapping:
    return MappingProxyType({})


@dataclass(frozen=True, slots=True)
class Hand:
    """A queue item made from a recording; open until queue.done or queue.spawned closes it, and again after
    queue.reopened."""

    qid: str
    intent: str
    project: str
    open: bool = True


@dataclass(frozen=True, slots=True)
class Claim:
    """One decision about a session's project: a rec.edited{project}, or a hand (qid set)."""

    project: str | None
    qid: str | None = None


@dataclass(frozen=True, slots=True)
class FilingLog:
    claims: Mapping[str, tuple[Claim, ...]] = field(default_factory=_empty)  # session -> claims in log order
    hands: Mapping[str, Hand] = field(default_factory=_empty)  # qid -> queue item with a from_item
    undone: frozenset[str] = frozenset()  # qids withdrawn by queue.done{by:"undo"}, not reopened since
    spawned: frozenset[str] = frozenset()  # qids the device started (queue.spawned)
    done: Mapping[str, bool] = field(default_factory=_empty)  # session -> its last rec.edited{done}
    shown: Mapping[str, str | None] = field(default_factory=_empty)  # session -> project of its latest note line
    filed_n: Mapping[str, int] = field(default_factory=_empty)  # session -> rec.filed records so far
    downgraded: frozenset[str] = frozenset()


@dataclass(frozen=True, slots=True)
class Known:
    """This Mac's projects.json, as the fold needs it: registered keys and slot -> key."""

    keys: frozenset[str] = frozenset()
    slots: Mapping[int, str] = field(default_factory=_empty)

    @classmethod
    def of(cls, projects: Iterable[Project]) -> Known:
        found = tuple(projects)
        return cls(frozenset(p.key for p in found), MappingProxyType({p.slot: p.key for p in found}))


@dataclass(frozen=True, slots=True)
class Filing:
    rec: str
    filing: str  # one of FILINGS
    project: str | None  # the effective project, registered here or not
    dest: str | None  # the effective destination
    hand: Hand | None  # the live hand, if any
    done: bool
    asked: str | None  # a new session's key this Mac does not know (the orphan group); None otherwise

    @property
    def orphan(self) -> bool:
        return self.asked is not None


# --- the fold -----------------------------------------------------------------------------------


def _str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def is_key(value: Any) -> bool:
    """A project key's shape (projects.registry), registered on this Mac or not."""
    return matches(LIMITS["project_key_pattern"], value)


def _edit_ok(e: Record) -> bool:
    return index.CHECKS["rec.edited"](e) and isinstance(e.get("done", False), bool)


def _hand_ok(e: Record) -> bool:
    return all(isinstance(e.get(k), str) for k in ("qid", "project", "intent", "from_item"))


def _reopened(e: Record) -> bool:
    """A queue.reopened the queue fold takes: the item's fields as queue.added had them (from_item is not kept)."""
    return e.get("ev") == REOPENED and all(isinstance(e.get(k), str) for k in ("qid", "project", "text", "intent"))


def _claim(claims: Mapping[str, tuple[Claim, ...]], rec: str, claim: Claim) -> dict[str, tuple[Claim, ...]]:
    return {**claims, rec: (*claims.get(rec, ()), claim)}


def _ordered(base: FilingLog, batch: tuple[Record, ...]) -> tuple[Mapping, Mapping, frozenset[str]]:
    """(claims, hands, undone): the parts of the fold where the order inside the batch matters."""
    claims, hands, undone = base.claims, base.hands, base.undone
    for e in batch:
        ev = e.get("ev")
        if ev == "rec.edited" and "project" in e and _edit_ok(e):
            claims = _claim(claims, e["rec"], Claim(e["project"]))
        elif ev == "queue.added" and _hand_ok(e) and e["qid"] not in hands:
            hands = {**hands, e["qid"]: Hand(e["qid"], e["intent"], e["project"])}
            claims = _claim(claims, e["from_item"], Claim(e["project"], e["qid"]))
        elif ev in CLOSING and _str(e.get("qid")) in hands:
            hands = {**hands, e["qid"]: replace(hands[e["qid"]], open=False)}
        elif _reopened(e) and e["qid"] in hands:
            hands = {**hands, e["qid"]: replace(hands[e["qid"]], open=True)}
            undone = undone - {e["qid"]}
        if ev == "queue.done" and _undo_ok(e):
            undone = undone | {e["qid"]}
    return MappingProxyType(dict(claims)), MappingProxyType(dict(hands)), undone


def _of(batch: tuple[Record, ...], evs: tuple[str, ...], ok: Callable[[Record], bool]) -> list[Record]:
    return [e for e in batch if e.get("ev") in evs and ok(e)]


def _done_ok(e: Record) -> bool:
    return "done" in e and _edit_ok(e)


def _undo_ok(e: Record) -> bool:
    return e.get("by") == "undo" and _str(e.get("qid")) is not None


def _has(key: str) -> Callable[[Record], bool]:
    return lambda e: _str(e.get(key)) is not None


def _counted(base: Mapping[str, int], recs: Iterable[str]) -> Mapping[str, int]:
    added = Counter(recs)
    return MappingProxyType({**base, **{rec: base.get(rec, 0) + n for rec, n in added.items()}})


def fold(events: Iterable[Record], prev: FilingLog | None = None) -> FilingLog:
    base, batch = prev or FilingLog(), tuple(e for e in events if isinstance(e, Mapping))
    claims, hands, undone = _ordered(base, batch)
    done = {e["rec"]: e["done"] for e in _of(batch, ("rec.edited",), _done_ok)}
    shown = {e["rec"]: _str(e.get("project")) for e in _of(batch, LINES, _has("rec"))}
    return FilingLog(
        claims=claims,
        hands=hands,
        undone=undone,
        spawned=base.spawned | {e["qid"] for e in _of(batch, ("queue.spawned",), _has("qid"))},
        done=MappingProxyType({**base.done, **done}),
        shown=MappingProxyType({**base.shown, **shown}),
        filed_n=_counted(base.filed_n, (e["rec"] for e in _of(batch, ("rec.filed",), _has("rec")))),
        downgraded=base.downgraded | {e["item"] for e in _of(batch, ("dest.downgraded",), _has("item"))},
    )


# --- what readers ask ---------------------------------------------------------------------------


def _live(log: FilingLog, rec: str) -> tuple[Claim, ...]:
    """The claims that count: a withdrawn hand does not, unless the device started it all the same."""
    return tuple(c for c in log.claims.get(rec, ()) if c.qid is None or c.qid not in log.undone or c.qid in log.spawned)


def _seg_project(seg: Record, known: Known) -> str | None:
    return _str(seg.get("project")) or known.slots.get(seg.get("slot"))


def recorded_project(idx: index.RecIndex, rec: str, known: Known) -> str | None:
    """The project the recording itself carries: a meeting's last landed segment, otherwise the manifest's."""
    archived = idx.archived.get(rec)
    segs = index.segments(idx, rec) if archived is None or archived.get("mode") == "meeting" else ()
    if segs:
        return _seg_project(segs[-1] if segs[0].get("mode") == "meeting" else segs[0], known)
    return _str(archived.get("project")) if archived else None


def effective_project(idx: index.RecIndex, log: FilingLog, rec: str, known: Known) -> str | None:
    live = _live(log, rec)
    return live[-1].project if live else recorded_project(idx, rec, known)


def hand_of(log: FilingLog, rec: str) -> Hand | None:
    qids = [c.qid for c in _live(log, rec) if c.qid is not None]
    return log.hands[qids[-1]] if qids else None


def dest_of(idx: index.RecIndex, log: FilingLog, rec: str) -> str | None:
    archived = idx.archived.get(rec) or {}
    return TODO if archived.get("dest") == TODO and rec not in log.downgraded else None


def _state(hidden: bool, hand: Hand | None, filed: bool, done: bool) -> str:
    if hidden:
        return "hidden"
    if hand is not None:
        return "handed"
    if filed:
        return "filed"
    return "done" if done else "new"


def filing_of(idx: index.RecIndex, log: FilingLog, rec: str, known: Known) -> Filing:
    project, hand, dest = effective_project(idx, log, rec, known), hand_of(log, rec), dest_of(idx, log, rec)
    done = log.done.get(rec, False)
    filing = _state(index.edit_of(idx, rec).hidden, hand, project in known.keys or dest is not None, done)
    asked = project if filing == "new" and is_key(project) and project not in known.keys else None
    return Filing(rec, filing, project, dest, hand, done, asked)


def filings(idx: index.RecIndex, log: FilingLog, known: Known) -> tuple[Filing, ...]:
    """Every archived session (a session still landing is not filed yet)."""
    return tuple(filing_of(idx, log, rec, known) for rec in sorted(idx.archived))


def projects(idx: index.RecIndex, log: FilingLog, known: Known) -> Mapping[str, str | None]:
    """The effective project of every session the log knows, archived or still landing."""
    recs = sorted(set(idx.archived) | {e["rec"] for e in idx.stored.values()})
    return MappingProxyType({rec: effective_project(idx, log, rec, known) for rec in recs})


# --- the daily note (docs/23 §2.4) --------------------------------------------------------------


def _title(idx: index.RecIndex, rec: str) -> str | None:
    found = (e.get("title") for e in reversed(idx.results.get(rec, ())))
    return next((t for t in found if isinstance(t, str) and t.strip()), None)


def _refile(f: Filing, log: FilingLog, known: Known) -> bool:
    return f.filing != "hidden" and f.project in known.keys and log.shown.get(f.rec) != f.project


def pending_filed(idx: index.RecIndex, log: FilingLog, known: Known, at: str) -> tuple[dict[str, Any], ...]:
    """rec.filed for each session filed after its daily-note line was written: it has a line (rec.result,
    rec.stale or an earlier rec.filed), it is not hidden, its project is registered here and its latest line
    shows another one. Once appended, the same state asks for nothing more. n numbers the session's rec.filed
    records (1, 2, ...), so filing back to an earlier project gets a line of its own (route: -f<n>-<key>)."""
    found = (filing_of(idx, log, rec, known) for rec in sorted(idx.archived) if rec in log.shown)
    return tuple(records.filed(rec=f.rec, project=f.project, session=idx.archived[f.rec], title=_title(idx, f.rec),
                               n=log.filed_n.get(f.rec, 0) + 1, at=at)
                 for f in found if _refile(f, log, known))  # fmt: skip
