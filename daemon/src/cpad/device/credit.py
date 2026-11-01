"""Host credit window (05 §6.3 rule 3): unanswered host-initiated bytes <= host_inflight_max.

An entry stops counting when its reply arrives or credit_reclaim_ms after it was sent,
exactly like protocol/tools/flows.py. Times are seconds on a monotonic clock.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

from cpad.protocol.limits import LIMITS

WINDOW = LIMITS["host_inflight_max"]
RECLAIM_S = LIMITS["credit_reclaim_ms"] / 1000


@dataclass(frozen=True, slots=True)
class Sent:
    id: str
    size: int
    t: float
    answered: bool = False


@dataclass(frozen=True, slots=True)
class Credit:
    entries: tuple[Sent, ...] = ()
    window: int = WINDOW
    reclaim_s: float = RECLAIM_S

    def _live(self, s: Sent, now: float) -> bool:
        return not s.answered and now < s.t + self.reclaim_s

    def used(self, now: float) -> int:
        return sum(s.size for s in self.entries if self._live(s, now))

    def can_send(self, size: int, now: float) -> bool:
        return self.used(now) + size <= self.window

    def sent(self, msg_id: str, size: int, now: float) -> Credit:
        return replace(self, entries=(*self.entries, Sent(msg_id, size, now)))

    def answered(self, msg_id: str) -> Credit:
        entries = tuple(replace(s, answered=True) if s.id == msg_id else s for s in self.entries)
        return self if entries == self.entries else replace(self, entries=entries)

    def prune(self, now: float) -> Credit:
        return replace(self, entries=tuple(s for s in self.entries if self._live(s, now)))
