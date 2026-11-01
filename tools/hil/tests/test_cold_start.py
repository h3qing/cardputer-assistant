"""harness.cold_start (issue #31): the leading muted run, Start -> Started from the device clock, the tone
at the start, and the per-take checks of rec_cold_start."""

import math

import pytest

from harness import cold_start as cs
from harness.mic_cal import MS, RATE


def tone(n: int, dbfs: float = -36.0, hz: float = 1000.0) -> list[int]:
    """A cosine: loud from its first sample."""
    amp = 10 ** (dbfs / 20) * 32767
    return [round(amp * math.cos(2 * math.pi * hz * i / RATE)) for i in range(n)]


def noise(n: int, seed: int = 1) -> list[int]:
    x, out = seed, []
    for _ in range(n):
        x = (x * 1103515245 + 12345) % 2**31
        out.append((x >> 16) % 61 - 30)  # room noise, |x| up to 30
    return out


# M5Unified's DC offset correction carried into a muted start: 35 down to 1, 210 samples (13.125 ms)
TAIL = [v for v in range(35, 0, -1) for _ in range(6)]


@pytest.mark.parametrize(
    ("x", "ms"),
    [
        ([0] * 320 + tone(1600), 20.0),  # exact zeros (DC correction near 0)
        ([1] * 160 + [500] + tone(800), 10.0),  # M5Unified's +1 DC offset
        ([-2, 2, 0] * 16 + [3], 3.0),  # anything within the firmware's kMutedMax (2)
        (tone(800, -20.0), 0.0),  # loud from the first sample
        ([], 0.0),
        ([0] * 32, 2.0),  # nothing but muted samples
        (TAIL + [1] * RATE + tone(800), 1013.125),  # the tail, then 1 s settled at +1
        ([-v for v in TAIL] + [-1] * 150 * MS + tone(800), 163.125),  # the same from a negative DC
        ([300] * 4 + [200] * 4 + tone(800), 0.0),  # a decaying start that does not reach the mute is live
    ],
)
def test_leading_muted_ms(x, ms):
    assert cs.leading_muted_ms(x) == ms


def test_start_to_started_is_an_upper_bound_from_the_device_clock():
    # G0 down at 1000 ms, Start at G0Long (1500), first written sample at 1506, Started one block later
    assert cs.start_to_started_ms(key_ms=1000, up_ms=1506) == 134
    assert cs.start_to_started_ms(key_ms=1000, up_ms=2500) == 1128


def test_the_tone_is_measured_over_the_first_250_ms():
    assert cs.tone_at_start_db(tone(cs.TONE_SPAN_MS * MS)) == pytest.approx(-36.0, abs=0.5)
    late = noise(cs.TONE_SPAN_MS * MS) + tone(RATE)  # the tone only after the span: not at the start
    assert cs.tone_at_start_db(late) < cs.TONE_FLOOR_DB


def take(label: str = "warm", muted: float = 0.0, started: int = 134, tone_db: float = -36.0) -> cs.Take:
    return cs.Take(label, samples=48000, muted_ms=muted, start_to_started_ms=started, tone_db=tone_db)


def by_name(checks):
    return {c["name"]: c["ok"] for c in checks}


def test_a_good_take_passes_every_check():
    assert by_name(cs.take_checks(take())) == {
        "warm_no_leading_mute": True,
        "warm_tone_from_the_start": True,
        "warm_started_within_2500ms": True,
    }


@pytest.mark.parametrize(
    ("t", "failed"),
    [
        (take(muted=20.0625), "warm_no_leading_mute"),
        (take(tone_db=-50.5), "warm_tone_from_the_start"),
        (take(started=2500), "warm_started_within_2500ms"),
    ],
)
def test_each_limit_fails_its_own_check(t, failed):
    checks = by_name(cs.take_checks(t))
    assert [n for n, ok in checks.items() if not ok] == [failed]


def test_the_cold_take_must_have_been_cold():
    # the hook muted the codec: the first live block came about 1 s after the Start
    assert by_name(cs.take_checks(take("cold", started=1130)))["cold_was_cold"] is True
    assert by_name(cs.take_checks(take("cold", started=400)))["cold_was_cold"] is False


def test_the_power_on_take_must_find_the_codec_warmed():
    assert by_name(cs.take_checks(take("power_on", started=420)))["power_on_warm_up_worked"] is True
    assert by_name(cs.take_checks(take("power_on", started=1130)))["power_on_warm_up_worked"] is False


def test_a_tail_and_a_short_mute_fail_the_power_on_take():
    # a partial warm-up: 80 ms still muted after the tail; the tone and Start -> Started look fine
    x = TAIL + [1] * 80 * MS + tone(RATE)
    checks = by_name(cs.take_checks(cs.take_of("power_on", x, key_ms=0, up_ms=520)))
    assert [n for n, ok in checks.items() if not ok] == ["power_on_no_leading_mute"]


def test_take_of_measures_samples():
    x = [0] * 160 + tone(RATE)
    t = cs.take_of("cold", x, key_ms=0, up_ms=1500)
    assert (t.label, t.samples, t.muted_ms, t.start_to_started_ms) == ("cold", 16160, 10.0, 1128)
    assert t.tone_db == pytest.approx(-36.0, abs=1.5)
    assert t.doc() == {
        "label": "cold",
        "seconds": 1.01,
        "muted_ms": 10.0,
        "start_to_started_ms": 1128,
        "tone_db": round(t.tone_db, 2),
    }
