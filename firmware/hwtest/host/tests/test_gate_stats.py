import pytest

from gate_stats import kbs, minutes_per_hour, percentile, summarize


def test_percentile_nearest_rank():
    vals = [5, 1, 4, 2, 3]
    assert percentile(vals, 50) == 3
    assert percentile(vals, 95) == 5
    assert percentile(vals, 0) == 1
    assert percentile([7], 95) == 7


def test_percentile_of_nothing_is_none():
    assert percentile([], 50) is None


def test_summarize_shape():
    s = summarize([10, 20, 30, 40])
    assert s == {"n": 4, "p50": 20, "p95": 40, "max": 40, "mean": 25.0}
    assert summarize([]) == {"n": 0}


def test_kbs_uses_1024():
    assert kbs(2048, 2.0) == pytest.approx(1.0)
    assert kbs(100, 0) is None


def test_minutes_per_recorded_hour():
    # 115,200,000 B per hour of 16 kHz mono (11 §4.4); 128 KB/s -> 14.6 min (audio-capture §7.2)
    assert minutes_per_hour(128) == pytest.approx(14.65, abs=0.01)
    assert minutes_per_hour(0) is None
