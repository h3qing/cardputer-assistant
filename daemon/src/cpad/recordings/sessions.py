"""Per-session state for `cpa rec ls / show` and the `recordings` block of `cpa status` (docs/11 §6.9). Pure.

States: on_device | pulling | archived | processing | blocked | done | transcript | failed | stale.
Rows carry ids, times, paths and states only, never a title or any text.
v0.4.1 (docs/13 §8): rows also carry the project key (the device's tag wins over the recorded one), the
number of star marks, starred and hidden (index.Edit), and come newest first (newest_first). A session still on
the device starts when wall.resolve_wall says it will once landed (the device clock, or the current boot's Seen).
docs/23 §2.1: the project is the effective one (filing.fold.projects: session -> project) when the caller gives
it, so a hand, a meeting's last segment and an older cpad's row without a project key all read the same.
"""

from __future__ import annotations

import posixpath
from collections.abc import Iterable, Mapping
from types import MappingProxyType
from typing import Any

from cpad.protocol.file_messages import FileItem
from cpad.recordings import index
from cpad.recordings.wall import Seen, resolve_wall

Status = tuple[str, str | None]
FAILED = ("failed", "stale")
NONE: Mapping[str, str | None] = MappingProxyType({})


def state_of(idx: index.RecIndex, rec: str, statuses: Mapping[str, Status]) -> tuple[str, str | None]:
    if rec not in idx.archived:
        return "pulling", None
    done = [e for e in idx.results.get(rec, ()) if e["state"] == "done"]
    if done:
        return ("done" if done[-1].get("level") == "summary" else "transcript"), None
    status = statuses.get(rec)
    if status is not None:
        return ("blocked", status[1]) if status[0] == "blocked" else ("processing", None)
    if "failed" in index.result_states(idx, rec):
        return "failed", None
    return ("stale" if rec in idx.stale else "archived"), None


def _result(idx: index.RecIndex, rec: str) -> dict[str, Any] | None:
    latest = index.latest_result(idx, rec)
    if latest is None:
        return None
    return {"state": latest["state"], "level": latest.get("level"), "error_code": latest.get("error_code")}


def _stored_row(idx: index.RecIndex, rec: str, statuses: Mapping[str, Status]) -> dict[str, Any]:
    segs, archived = index.segments(idx, rec), idx.archived.get(rec)
    first = segs[0] if segs else {}
    state, reason = state_of(idx, rec, statuses)
    source = archived or {
        "mode": first.get("mode"),
        "project": segs[-1].get("project") if segs else None,  # the last tag wins, as in the manifest (#46)
        "started_ms": first.get("wall_ms"),
        "uncertain": first.get("uncertain"),
        "dir": posixpath.dirname(first.get("file", "")),
    }
    dur = archived.get("dur_ms") if archived else sum((s["size"] - 44) // 32 for s in segs)
    return {"sid": rec, "mode": source.get("mode"), "started_ms": source.get("started_ms"),
            "uncertain": bool(source.get("uncertain")), "dur_ms": dur, "state": state, "reason": reason,
            "dir": source.get("dir"), "segments": archived.get("segments") if archived else len(segs),
            "gaps": list(archived.get("gaps") or ()) if archived else [], "result": _result(idx, rec),
            "audio_swept": rec in idx.swept, "project": source.get("project"),
            "marks": sum(len(s.get("stars") or ()) for s in segs)}  # fmt: skip


def _device_start(item: FileItem, seen: Seen | None) -> int | None:
    """The start it will land with when that is known now (resolve_wall cases 1, 2); landing time is not."""
    found = resolve_wall(item.at, item.id, seen, 0)
    return None if found.uncertain else found.wall_ms


def _device_row(rec: str, items: list[FileItem], current: str | None, seen: Seen | None) -> dict[str, Any]:
    first, last = items[0], max(items, key=lambda i: i.seg)
    wall = _device_start(first, seen)
    pulling = current is not None and any(i.id == current for i in items)
    return {"sid": rec, "mode": first.mode, "started_ms": wall, "uncertain": wall is None,
            "dur_ms": sum(i.dur_ms for i in items), "state": "pulling" if pulling else "on_device", "reason": None,
            "dir": None, "segments": len(items), "gaps": [], "result": None, "audio_swept": False,
            "project": last.project, "marks": sum(len(i.stars) for i in items)}  # fmt: skip


def _edited(row: Mapping[str, Any], edit: index.Edit, projects: Mapping[str, str | None]) -> dict[str, Any]:
    tagged = edit.project if edit.tagged else row.get("project")
    return {**row, "project": projects.get(row["sid"], tagged), "starred": edit.starred, "hidden": edit.hidden}


def with_edits(found: Iterable[Mapping[str, Any]], idx: index.RecIndex,
               projects: Mapping[str, str | None] = NONE) -> tuple[dict[str, Any], ...]:  # fmt: skip
    """Rows with the starred / hidden / tagged project of idx applied (the effective project when `projects` has
    the session), newest first; applying it twice is harmless, so `cpa rec ls` can apply the log it reads to rows
    a running cpad sent."""
    return tuple(sorted((_edited(row, index.edit_of(idx, row["sid"]), projects) for row in found), key=newest_first))


def newest_first(row: Mapping[str, Any]) -> tuple[int, int, str]:
    """Sort key (docs/13 §8.1): newest start first, then the session id. MonoIds only order sessions within
    one epoch, so they never decide alone. started_ms is the device clock or, when the device had none, the
    landing time (wall.resolve_wall); only a session still on the device can lack it, and it sorts first, but
    landing then puts it at the landing time, newer than any row a page already passed."""
    started = row.get("started_ms")
    return (0, 0, row["sid"]) if started is None else (1, -started, row["sid"])


def rows(idx: index.RecIndex, *, statuses: Mapping[str, Status], listing: Iterable[FileItem] = (),
         current: str | None = None, seen: Seen | None = None,
         projects: Mapping[str, str | None] = NONE) -> tuple[dict[str, Any], ...]:  # fmt: skip
    known = {e["rec"] for e in idx.stored.values()} | set(idx.archived)
    device: dict[str, list[FileItem]] = {}
    for item in listing:
        if item.rec not in known:
            device.setdefault(item.rec, []).append(item)
    out = [_stored_row(idx, rec, statuses) for rec in known]
    out += [_device_row(rec, items, current, seen) for rec, items in device.items()]
    return with_edits(out, idx, projects)


def summary(found: Iterable[Mapping[str, Any]], *, idx: index.RecIndex, on_device: int | None,
            pulling: bool) -> dict[str, Any]:  # fmt: skip
    states = [row["state"] for row in found]
    last = max((e.get("at", "") for results in idx.results.values() for e in results), default=None)
    return {"on_device": on_device, "pulling": int(pulling), "waiting": states.count("archived"),
            "processing": states.count("processing"), "blocked": states.count("blocked"),
            "failed": sum(states.count(s) for s in FAILED), "last_result_at": last}  # fmt: skip


def find(found: Iterable[Mapping[str, Any]], sid: str) -> Mapping[str, Any] | None:
    return next((row for row in found if row["sid"].lower() == sid.lower()), None)
