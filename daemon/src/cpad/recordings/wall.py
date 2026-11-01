"""Wall-clock time of a recording segment (docs/11 §6.5). Pure.

1. the device knew the wall clock (at.wall_ms): use it;
2. the segment was recorded in the boot the link is talking to now and at.up_ms is known:
   wall = wall at the last hello/pong - (up_ms at that hello/pong - at.up_ms);
3. otherwise the landing time, marked uncertain (the daily note gets a '~', 05 §7.8).
"""

from __future__ import annotations

from dataclasses import dataclass

from cpad.protocol.file_messages import At
from cpad.protocol.limits import LIMITS

BOOT_LEN = 13  # EEEEEEE-BBBBB


@dataclass(frozen=True, slots=True)
class Seen:
    """The device's boot and up_ms as of host wall time wall_ms (from the latest hello / pong)."""

    boot: str
    up_ms: int
    wall_ms: int


@dataclass(frozen=True, slots=True)
class Wall:
    wall_ms: int
    uncertain: bool


def boot_of(monoid: str) -> str:
    return monoid[:BOOT_LEN]


def resolve_wall(at: At | None, seg_id: str, seen: Seen | None, landed_ms: int) -> Wall:
    if at is not None and at.wall_ms is not None:
        return Wall(at.wall_ms, False)
    if at is not None and seen is not None and seen.boot == boot_of(seg_id) and at.up_ms <= seen.up_ms:
        wall = seen.wall_ms - (seen.up_ms - at.up_ms)
        if wall >= LIMITS["epoch_ms_min"]:
            return Wall(wall, False)
    return Wall(landed_ms, True)
