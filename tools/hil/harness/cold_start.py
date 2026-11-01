"""Pure checks of rec_cold_start (issue #31): the first recording after a codec power-on.

A take is one memo held while the Mac's speakers play a continuous 1 kHz tone: its samples once pulled,
the device time G0 went down (debug.state key_ms) and segment 0's at.up_ms, which since v0.3.2 is the
device time of the first written sample (the audio task drops what the codec still holds muted). Checks:
  - the leading muted run (|x| <= MUTED_MAX, the firmware's rec::kMutedMax: exact zeros, or M5Unified's
    DC offset correction settled at +-1, after its decaying tail when the offset was carried from an
    earlier capture) is at most MUTED_LIMIT_MS;
  - the tone is heard from the start: 1 kHz over the first TONE_SPAN_MS at TONE_FLOOR_DB or more (the
    speakers reached the device at about -36 dBFS when issue #31 was measured);
  - Start -> Started stays under STARTED_LIMIT_MS (the firmware starts anyway after 2 s). The Start is
    posted at G0Long (G0 down + G0_LONG_MS); Started comes with the block that holds the first written
    sample, at most one block (BLOCK_MS) after that sample, so up_ms + BLOCK_MS - (key_ms + G0_LONG_MS)
    is an upper bound;
  - the cold take was cold (Start -> Started at least COLD_MIN_MS: the hook really muted the codec, or the
    take proves nothing), and the power-on take found the codec warmed (under WARM_UP_LIMIT_MS: the boot's
    warm-up removed the muted start; the Start may still wait for the warm-up's end).
Standard library only.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from typing import Any

from harness.mic_cal import MS, tone_db
from harness.report import check

MUTED_MAX = 2
MUTED_LIMIT_MS = 20.0
G0_LONG_MS = 500
BLOCK_MS = 128  # 2048 samples, the ring's block
STARTED_LIMIT_MS = 2500
COLD_MIN_MS = 600  # a soft-reset codec mutes 933-995 ms (issue #31 bench)
WARM_UP_LIMIT_MS = 800
TONE_SPAN_MS = 250
TONE_FLOOR_DB = -50.0


def _decays(prev: int, v: int) -> bool:
    """The tail never grows and falls by at most 1/16 of itself and 1 per sample; a tone falls faster."""
    a, b = abs(prev), abs(v)
    return b <= a and a - b <= a // 16 + 1


def dc_tail(x: Sequence[int]) -> int:
    """Samples of M5Unified's DC offset correction tail at the start, as the firmware finds it (rec::tail):
    louder than MUTED_MAX, of one sign, decaying into a quiet sample; 0 otherwise."""
    n = 0
    while n < len(x) and abs(x[n]) > MUTED_MAX:
        if n and ((x[n] < 0) != (x[n - 1] < 0) or not _decays(x[n - 1], x[n])):
            return 0
        n += 1
    return n if 0 < n < len(x) and _decays(x[n - 1], x[n]) else 0


def leading_muted_ms(x: Sequence[int]) -> float:
    """How long the recording starts muted: the DC tail, if any, and the samples after it up to the first one
    louder than MUTED_MAX."""
    n = next((i for i in range(dc_tail(x), len(x)) if abs(x[i]) > MUTED_MAX), len(x))
    return n / MS


def start_to_started_ms(key_ms: int, up_ms: int) -> int:
    """Upper bound of Start -> Started on the device clock (see the module docstring)."""
    return up_ms + BLOCK_MS - (key_ms + G0_LONG_MS)


def tone_at_start_db(x: Sequence[int]) -> float:
    return tone_db(x[: TONE_SPAN_MS * MS])


@dataclass(frozen=True, slots=True)
class Take:
    label: str  # cold / warm / power_on
    samples: int
    muted_ms: float
    start_to_started_ms: int
    tone_db: float

    def doc(self) -> dict[str, Any]:
        return {
            "label": self.label,
            "seconds": round(self.samples / (1000 * MS), 2),
            "muted_ms": self.muted_ms,
            "start_to_started_ms": self.start_to_started_ms,
            "tone_db": round(self.tone_db, 2),
        }


def take_of(label: str, x: Sequence[int], key_ms: int, up_ms: int) -> Take:
    return Take(label, len(x), leading_muted_ms(x), start_to_started_ms(key_ms, up_ms), tone_at_start_db(x))


def take_checks(t: Take) -> list[dict[str, Any]]:
    started, name = t.start_to_started_ms, t.label
    extra = {
        "cold": (check("cold_was_cold", started >= COLD_MIN_MS, start_to_started_ms=started),),
        "power_on": (check("power_on_warm_up_worked", started < WARM_UP_LIMIT_MS, start_to_started_ms=started),),
    }
    return [
        check(f"{name}_no_leading_mute", t.muted_ms <= MUTED_LIMIT_MS, muted_ms=t.muted_ms),
        check(f"{name}_tone_from_the_start", t.tone_db >= TONE_FLOOR_DB, tone_db=round(t.tone_db, 2)),
        check(f"{name}_started_within_{STARTED_LIMIT_MS}ms", started < STARTED_LIMIT_MS, start_to_started_ms=started),
        *extra.get(name, ()),
    ]
