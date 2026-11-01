"""Fold of the rec.* events in events.jsonl (the only authority). Pure; records are never mutated.

Malformed records (missing or mistyped keys) are skipped, like the queue fold does.
v0.4.1 (docs/13 §8.2, §8.4): rec.edited / rec.hidden / rec.unhidden fold, in log order, into one Edit per
session: starred, the project set on the device (tagged; None = no project) and hidden.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from types import MappingProxyType
from typing import Any

Record = Mapping[str, Any]


def _empty() -> Mapping:
    return MappingProxyType({})


@dataclass(frozen=True, slots=True)
class Edit:
    starred: bool = False
    tagged: bool = False  # project below replaces the recorded one, even when it is None
    project: str | None = None
    hidden: bool = False


@dataclass(frozen=True, slots=True)
class RecIndex:
    stored: Mapping[tuple[str, str], Record] = field(default_factory=_empty)  # (dev, seg id) -> rec.seg.stored
    deleted: frozenset[tuple[str, str]] = frozenset()
    archived: Mapping[str, Record] = field(default_factory=_empty)  # session id -> rec.archived
    results: Mapping[str, tuple[Record, ...]] = field(default_factory=_empty)
    stale: frozenset[str] = frozenset()
    swept: frozenset[str] = frozenset()
    edits: Mapping[str, Edit] = field(default_factory=_empty)


def _strs(e: Record, *keys: str) -> bool:
    return all(isinstance(e.get(k), str) for k in keys)


CHECKS = {
    "rec.seg.stored": lambda e: _strs(e, "dev", "id", "rec", "file", "mode") and isinstance(e.get("seg"), int),
    "rec.seg.deleted": lambda e: _strs(e, "dev", "id"),
    "rec.archived": lambda e: _strs(e, "rec", "dir", "mode"),
    "rec.result": lambda e: _strs(e, "rec", "state"),
    "rec.stale": lambda e: _strs(e, "rec"),
    "rec.audio.swept": lambda e: _strs(e, "rec"),
    "rec.edited": lambda e: (
        _strs(e, "rec") and isinstance(e.get("starred", False), bool) and isinstance(e.get("project"), str | None)
    ),
    "rec.hidden": lambda e: _strs(e, "rec"),
    "rec.unhidden": lambda e: _strs(e, "rec"),
}
EDITS = ("rec.edited", "rec.hidden", "rec.unhidden")


def _of(events: tuple[Record, ...], ev: str) -> list[Record]:
    return [e for e in events if e.get("ev") == ev and CHECKS[ev](e)]


def _edit(prev: Edit, e: Record) -> Edit:
    if e["ev"] != "rec.edited":
        return replace(prev, hidden=e["ev"] == "rec.hidden")
    tag = {"tagged": True, "project": e["project"]} if "project" in e else {}
    return replace(prev, starred=e.get("starred", prev.starred), **tag)


def _edits(base: Mapping[str, Edit], batch: tuple[Record, ...]) -> Mapping[str, Edit]:
    edits = base
    for e in batch:
        if e.get("ev") in EDITS and CHECKS[e["ev"]](e):
            edits = {**edits, e["rec"]: _edit(edits.get(e["rec"], Edit()), e)}
    return MappingProxyType(edits)


def fold(events: Iterable[Record], prev: RecIndex | None = None) -> RecIndex:
    base, batch = prev or RecIndex(), tuple(events)
    results = dict(base.results)
    for e in _of(batch, "rec.result"):
        results = {**results, e["rec"]: (*results.get(e["rec"], ()), e)}
    return RecIndex(
        stored=MappingProxyType({**base.stored, **{(e["dev"], e["id"]): e for e in _of(batch, "rec.seg.stored")}}),
        deleted=base.deleted | {(e["dev"], e["id"]) for e in _of(batch, "rec.seg.deleted")},
        archived=MappingProxyType({**base.archived, **{e["rec"]: e for e in _of(batch, "rec.archived")}}),
        results=MappingProxyType(results),
        stale=base.stale | {e["rec"] for e in _of(batch, "rec.stale")},
        swept=base.swept | {e["rec"] for e in _of(batch, "rec.audio.swept")},
        edits=_edits(base.edits, batch),
    )


def stored_ids(idx: RecIndex, dev: str) -> frozenset[str]:
    return frozenset(seg for d, seg in idx.stored if d == dev)


def segments(idx: RecIndex, rec: str) -> tuple[Record, ...]:
    """The session's stored segments in segment order (the latest record per segment index)."""
    by_seg = {e["seg"]: e for e in idx.stored.values() if e["rec"] == rec}
    return tuple(by_seg[k] for k in sorted(by_seg))


def open_sessions(idx: RecIndex) -> tuple[str, ...]:
    """Sessions with stored segments and no manifest yet."""
    return tuple(sorted({e["rec"] for e in idx.stored.values()} - set(idx.archived)))


def result_states(idx: RecIndex, rec: str) -> frozenset[str]:
    return frozenset(e["state"] for e in idx.results.get(rec, ()))


def latest_result(idx: RecIndex, rec: str) -> Record | None:
    found = idx.results.get(rec, ())
    return found[-1] if found else None


def edit_of(idx: RecIndex, rec: str) -> Edit:
    return idx.edits.get(rec, Edit())
