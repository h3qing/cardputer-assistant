"""Inbox: does a session need you? (05 §7.6, US-07)

since is when the daemon first saw the current (sid, status); it restarts when status changes.
need = registered project AND (waiting, or idle for >= inbox_idle_s) AND (sid, status, since) not dismissed;
waiting includes a background session in state blocked that is not running (see waiting()).
Dismissals live in memory only and expire by themselves once the status changes.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass

from cpad.agents.base import SessionInfo

Dismissed = frozenset[tuple[str, str, float]]


@dataclass(frozen=True, slots=True)
class Tracked:
    info: SessionInfo
    project: str | None
    since: float

    @property
    def key(self) -> tuple[str, str, float]:
        return (self.info.sid, self.info.status, self.since)


def track(
    prev: Iterable[Tracked], current: Iterable[tuple[SessionInfo, str | None]], now: float
) -> tuple[Tracked, ...]:
    known = {(t.info.session_id, t.info.status): t.since for t in prev}
    return tuple(Tracked(info, project, known.get((info.session_id, info.status), now)) for info, project in current)


def waiting(info: SessionInfo) -> bool:
    """Waiting on the user right now: status waiting (permission prompt, question, dialog), or a background
    session the CLI marks blocked while it is not running (a failed turn, e.g. logged out). G4, #5."""
    blocked = info.kind == "bg" and info.state == "blocked" and info.status not in ("busy", "shell")
    return info.status == "waiting" or blocked


def needs(t: Tracked, now: float, idle_s: float, dismissed: Dismissed) -> bool:
    if t.project is None or t.key in dismissed:
        return False
    return waiting(t.info) or (t.info.status == "idle" and now - t.since >= idle_s)


def dismiss(dismissed: Dismissed, t: Tracked) -> Dismissed:
    return dismissed | {t.key}


def prune(dismissed: Dismissed, tracked: Iterable[Tracked]) -> Dismissed:
    live = {t.key for t in tracked}
    return frozenset(d for d in dismissed if d in live)
