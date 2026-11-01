"""The writes of `cpa filing` (docs/23 §2.2, §3.3, §3.4): file, done, hide, reopen, hand. Pure.

plan() decides from events.jsonl alone, so the shell can decide again under the events lock with whatever another
process appended meanwhile, and write exactly what that decision says:
- one record or none (rule 2): file -> rec.edited{project, by:"cli"}; done -> rec.edited{done:true, by:"cli"};
  hide -> rec.hidden{by:"cli"}; reopen -> the one reason of the current state taken back: rec.unhidden,
  queue.done{qid, by:"undo"} for a hand not yet spawned or done, rec.edited{done:false}; hand -> one
  queue.added{origin:"inbox", from_item, voice:true, bytes, sha8} for KEY or the effective project, its text the
  memo text or else the transcript the shell read, through queue_text (never argv), started only from the device
  board (ADR 0002), always in plan mode (hub.reduce);
- already in that state: nothing (changed false, no undo) (rule 3);
- refused (FilingError): an unknown id (E_UNKNOWN_ID); a key this Mac does not know (E_UNKNOWN_PROJECT), except
  a key the recording itself carried (recorded, or set on it before and since removed here), which only an undo
  restores; the other-Mac group without --other (E_BAD_ARG other); done on a recording that is not new; file or
  hide under an open queue item, which keeps its project (withdraw it first: reopen); reopen of a spawned hand
  or of a filed recording; hand of a hidden one or under an open queue item (E_ALREADY); hand of a meeting,
  whose words are partly other people's (E_BAD_ARG id, D16), or of one without usable text (E_NO_TEXT);
- the undo is a neutral object (rule 6) whose own plan gives the state before back: file -> file back to the
  project before (no project when that was no key at all); done, hide, hand -> reopen; reopen -> done or hide; a
  withdrawn hand, or an unhide that hide would refuse (an open queue item), has none. Undone newest first, a
  stack of them walks back through every state before.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from cpad.events import records as queue_records
from cpad.filing import fold, rows
from cpad.filing.args import (
    E_ALREADY,
    E_BAD_ARG,
    E_NO_TEXT,
    E_UNKNOWN_ID,
    E_UNKNOWN_PROJECT,
    VERSION,
    FilingError,
    parse_id,
    parse_key,
)
from cpad.filing.text import queue_text
from cpad.queue.fold import fold_queue, next_qid
from cpad.recordings import index, records

BY = "cli"


@dataclass(frozen=True, slots=True)
class Ask:
    op: str  # file | done | hide | reopen | hand (DECIDE)
    id: str  # as typed
    project: str | None = None  # file, hand: the key
    none: bool = False  # file --none
    other: bool = False  # file, hand --other: yes, this recording asked for another Mac's project
    spawn: bool = False  # hand --spawn: queued to be started as a session (intent spawn), still on the device


@dataclass(frozen=True, slots=True)
class Plan:
    rec: str
    records: tuple[dict[str, Any], ...]
    undo: dict[str, Any] | None


@dataclass(frozen=True, slots=True)
class _State:
    idx: index.RecIndex
    log: fold.FilingLog
    known: fold.Known
    f: fold.Filing
    events: Sequence[Mapping[str, Any]]
    transcript: str | None  # hand: the recording's transcript, read by the shell


def _undo(op: str, rec: str, **args: Any) -> dict[str, Any]:
    return {"op": op, "id": rec, "args": args}


def _target(ask: Ask) -> str | None:
    if (ask.project is None) == (not ask.none):
        raise FilingError(E_BAD_ARG, "project")
    return None if ask.none else parse_key(ask.project)


def _own_keys(s: _State) -> frozenset[str | None]:
    """The keys the recording itself carried: the recorded one, and every one set on it since."""
    claimed = (c.project for c in s.log.claims.get(s.f.rec, ()))
    return frozenset((fold.recorded_project(s.idx, s.f.rec, s.known), *claimed))


def _file(s: _State, ask: Ask, at: str) -> Plan:
    key, rec = _target(ask), s.f.rec
    if key is not None and key not in s.known.keys and key not in _own_keys(s):
        raise FilingError(E_UNKNOWN_PROJECT, "project")
    if s.f.project == key:
        return Plan(rec, (), None)
    if s.f.hand is not None and s.f.hand.open:
        raise FilingError(E_ALREADY, "queue")  # the queue item keeps its project: withdraw it first (reopen)
    if s.f.orphan and not ask.other:
        raise FilingError(E_BAD_ARG, "other")
    before = s.f.project if fold.is_key(s.f.project) else None
    return Plan(rec, (records.edited_project(rec=rec, project=key, at=at, by=BY),),
                _undo("file", rec, project=before))  # fmt: skip


def _done(s: _State, ask: Ask, at: str) -> Plan:
    rec = s.f.rec
    if s.f.filing == "done":
        return Plan(rec, (), None)
    if s.f.filing != "new":
        raise FilingError(E_ALREADY, "filing")  # filed, handed or hidden already says more than done
    return Plan(rec, (records.edited_done(rec=rec, done=True, at=at, by=BY),), _undo("reopen", rec))


def _hide(s: _State, ask: Ask, at: str) -> Plan:
    rec = s.f.rec
    if s.f.filing == "hidden":
        return Plan(rec, (), None)
    if s.f.hand is not None and s.f.hand.open:
        raise FilingError(E_ALREADY, "queue")
    return Plan(rec, (records.hidden(rec=rec, at=at, by=BY),), _undo("reopen", rec))


def _withdraw(s: _State, at: str) -> Plan:
    hand = s.f.hand
    if hand is None or not hand.open:
        raise FilingError(E_ALREADY, "queue")
    return Plan(s.f.rec, (queue_records.queue_done(hand.qid, at, by="undo"),), None)


def _reopen(s: _State, ask: Ask, at: str) -> Plan:
    rec, state = s.f.rec, s.f.filing
    if state == "hidden":
        hide_back = s.f.hand is None or not s.f.hand.open  # hide refuses an open queue item
        return Plan(rec, (records.unhidden(rec=rec, at=at, by=BY),), _undo("hide", rec) if hide_back else None)
    if state == "handed":
        return _withdraw(s, at)
    if state == "done":
        return Plan(rec, (records.edited_done(rec=rec, done=False, at=at, by=BY),), _undo("done", rec))
    if state == "filed":
        raise FilingError(E_ALREADY, "filing")  # a project, not a state: `file ID --none`
    return Plan(rec, (), None)


def _hand_project(s: _State, ask: Ask) -> str:
    """KEY, or else the effective project; either must be this Mac's, and the other-Mac group needs --other."""
    key = s.f.project if ask.project is None else ask.project
    if key is None or key not in s.known.keys:
        raise FilingError(E_UNKNOWN_PROJECT, "project")
    if s.f.orphan and not ask.other:
        raise FilingError(E_BAD_ARG, "other")
    return key


def _hand(s: _State, ask: Ask, at: str) -> Plan:
    rec = s.f.rec
    if s.idx.archived[rec].get("mode") != "memo":
        raise FilingError(E_BAD_ARG, "id")  # a meeting holds other people's words (D16)
    if s.f.filing == "hidden":
        raise FilingError(E_ALREADY, "filing")
    if s.f.hand is not None and s.f.hand.open:
        raise FilingError(E_ALREADY, "queue")
    key = _hand_project(s, ask)
    memo = rows.done_result(s.idx, rec).get("memo_text")
    text = queue_text(memo if isinstance(memo, str) and memo.strip() else s.transcript)
    if text is None:
        raise FilingError(E_NO_TEXT, "text")
    return Plan(rec, (queue_records.queue_handed(qid=next_qid(fold_queue(s.events)), project=key, text=text,
                                                 intent="spawn" if ask.spawn else "todo", rec=rec, at=at),),
                _undo("reopen", rec))  # fmt: skip


DECIDE: Mapping[str, Callable[[_State, Ask, str], Plan]] = {"file": _file, "done": _done, "hide": _hide,
                                                            "reopen": _reopen, "hand": _hand}  # fmt: skip


def _state(events: Sequence[Mapping[str, Any]], known: fold.Known, rec: str, transcript: str | None) -> _State:
    idx, log = index.fold(events), fold.fold(events)
    if rec not in idx.archived:
        raise FilingError(E_UNKNOWN_ID, "id")
    return _State(idx, log, known, fold.filing_of(idx, log, rec, known), events, transcript)


def plan(ask: Ask, events: Sequence[Mapping[str, Any]], known: fold.Known, at: str,
         transcript: str | None = None) -> Plan:  # fmt: skip
    rec = parse_id(ask.id)
    if ask.op == "file":
        _target(ask)  # a bad key is a bad argument whatever the recording
    elif ask.op == "hand" and ask.project is not None:
        parse_key(ask.project)
    return DECIDE[ask.op](_state(events, known, rec, transcript), ask, at)


def result(p: Plan, events: Sequence[Mapping[str, Any]], known: fold.Known, *, changed: bool) -> dict[str, Any]:
    """The protocol/filing/v1 result: the state and the queue item after `events` (which include what was
    written)."""
    idx, log = index.fold(events), fold.fold(events)
    f = fold.filing_of(idx, log, p.rec, known)
    return {"ok": True, "v": VERSION, "id": p.rec, "filing": f.filing, "changed": changed,
            "undo": p.undo if changed else None, "queue": rows.queue_of(f)}  # fmt: skip


def ask_of(undo: Mapping[str, Any]) -> Ask:
    """The Ask an undo object stands for."""
    project = undo["args"].get("project")
    return Ask(undo["op"], undo["id"], project=project, none=undo["op"] == "file" and project is None)


def undo_argv(undo: Mapping[str, Any]) -> list[str]:
    """The undo as cpa prints it; every part is an op word, a MonoId or a key."""
    base = ["cpa", "filing", undo["op"], undo["id"]]
    if undo["op"] != "file":
        return base
    project = undo["args"].get("project")
    return [*base, project if project else "--none"]
