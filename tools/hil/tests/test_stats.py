"""p95 and summaries: nearest-rank percentiles, as ADR-0001 §2 measured them."""

import pytest

from harness.stats import percentile, summary


def test_p95_of_ten_samples_is_the_maximum():
    assert percentile([5, 1, 9, 3, 7, 2, 8, 4, 6, 10], 95) == 10


def test_p95_of_one_hundred_samples_is_the_95th_smallest():
    assert percentile(list(range(1, 101)), 95) == 95


def test_p95_of_twenty_samples_is_the_19th_smallest():
    assert percentile(list(range(100, 120)), 95) == 118


def test_p50_nearest_rank_and_extremes():
    values = [3.0, 1.0, 2.0, 4.0]
    assert percentile(values, 50) == 2.0
    assert percentile(values, 100) == 4.0
    assert percentile(values, 1) == 1.0


def test_single_sample_is_every_percentile():
    assert percentile([42], 95) == 42


def test_percentile_input_is_not_reordered():
    values = [3, 1, 2]
    percentile(values, 95)
    assert values == [3, 1, 2]


@pytest.mark.parametrize("q", [0, -1, 101])
def test_percentile_rejects_out_of_range_q(q):
    with pytest.raises(ValueError):
        percentile([1, 2], q)


def test_percentile_rejects_no_samples():
    with pytest.raises(ValueError):
        percentile([], 95)


def test_summary_rounds_and_counts():
    s = summary([0.1234, 0.5, 0.25, 1.0], digits=2)
    assert s == {"n": 4, "min": 0.12, "p50": 0.25, "p95": 1.0, "max": 1.0}


def test_summary_of_nothing_is_only_a_count():
    assert summary([]) == {"n": 0}
