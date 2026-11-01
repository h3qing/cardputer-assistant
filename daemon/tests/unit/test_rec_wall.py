"""resolve_wall (docs/11 §6.5): device wall clock, else same-boot up_ms arithmetic, else landing time (~)."""

from __future__ import annotations

from cpad.protocol.file_messages import At
from cpad.recordings.wall import Seen, Wall, boot_of, resolve_wall

SEG = "K3M9Q2T-00042-0A1C"
LANDED = 1792620999000


def test_boot_of_is_the_monoid_prefix():
    assert boot_of(SEG) == "K3M9Q2T-00042"


def test_device_wall_clock_wins():
    seen = Seen(boot="K3M9Q2T-00042", up_ms=900000, wall_ms=1792620900000)
    assert resolve_wall(At(435000, 1792620120000), SEG, seen, LANDED) == Wall(1792620120000, False)


def test_same_boot_uses_the_up_ms_difference():
    seen = Seen(boot="K3M9Q2T-00042", up_ms=900000, wall_ms=1792620900000)
    assert resolve_wall(At(435000, None), SEG, seen, LANDED) == Wall(1792620900000 - 465000, False)


def test_other_boot_or_unknown_at_falls_back_to_landing_time():
    other = Seen(boot="K3M9Q2T-00043", up_ms=900000, wall_ms=1792620900000)
    assert resolve_wall(At(435000, None), SEG, other, LANDED) == Wall(LANDED, True)
    assert resolve_wall(None, SEG, Seen("K3M9Q2T-00042", 1, 1792620900000), LANDED) == Wall(LANDED, True)
    assert resolve_wall(At(435000), SEG, None, LANDED) == Wall(LANDED, True)


def test_an_impossible_same_boot_result_is_uncertain():
    seen = Seen(boot="K3M9Q2T-00042", up_ms=100, wall_ms=1735689600100)
    assert resolve_wall(At(435000, None), SEG, seen, LANDED) == Wall(LANDED, True)
