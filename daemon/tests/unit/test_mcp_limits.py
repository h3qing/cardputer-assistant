"""Output budget, field clipping and token buckets (docs/15 §3.8). Pure."""

from __future__ import annotations

import json

import pytest

from cpad.mcp import limits
from cpad.mcp.limits import Bucket


def test_json_size_counts_utf8_bytes_of_compact_json():
    assert limits.json_size({"a": "汉"}) == len('{"a":"汉"}'.encode())


def test_fit_keeps_everything_when_small():
    kept, truncated = limits.fit(({"n": i} for i in range(5)), {"trust": "x"}, "items")
    assert kept == tuple({"n": i} for i in range(5)) and not truncated


def test_fit_stops_before_the_budget_and_says_so():
    items = [{"text": "汉" * 1000} for _ in range(20)]
    kept, truncated = limits.fit(items, {"trust": "x"}, "items", budget=10_000)
    assert truncated and 0 < len(kept) < 20
    doc = {"trust": "x", "items": list(kept), "next_cursor": "c" * 40, "truncated": True}
    assert limits.json_size(doc) <= 10_000


def test_fit_with_budget_too_small_for_one_item_keeps_none():
    kept, truncated = limits.fit([{"text": "x" * 500}], {}, "items", budget=300)
    assert kept == () and truncated


def test_clip_marks_a_cut_and_leaves_short_text_alone():
    assert limits.clip("short", 10) == "short"
    assert limits.clip("x" * 20, 10) == "x" * 9 + "…"
    assert limits.clip(None, 10) is None


def test_bucket_allows_a_burst_then_refills_at_the_rate():
    rate = limits.Rate(per_min=60, burst=3)
    bucket = limits.full(rate, now=0.0)
    results = []
    for _ in range(4):
        bucket, ok = limits.take(bucket, 0.0, rate)
        results.append(ok)
    assert results == [True, True, True, False]
    assert limits.retry_after(bucket, rate) == pytest.approx(1.0)
    bucket, ok = limits.take(bucket, 1.0, rate)
    assert ok


def test_bucket_never_exceeds_the_burst():
    rate = limits.Rate(per_min=10, burst=3)
    bucket, ok = limits.take(Bucket(0.0, 0.0), 10_000.0, rate)
    assert ok and bucket.tokens == pytest.approx(2.0)


def test_bucket_tolerates_a_clock_going_backwards():
    rate = limits.Rate(per_min=60, burst=2)
    bucket, ok = limits.take(Bucket(1.0, 100.0), 50.0, rate)
    assert ok and bucket.tokens == pytest.approx(0.0)


def test_default_rates_match_the_design():
    assert (limits.READ.per_min, limits.READ.burst) == (60, 20)
    assert (limits.WRITE.per_min, limits.WRITE.burst) == (10, 3)
    assert limits.OUTPUT_MAX == 24 * 1024


def test_dumps_is_the_compact_form_used_for_sizes():
    doc = {"a": [1, "汉"]}
    assert limits.dumps(doc) == json.dumps(doc, ensure_ascii=False, separators=(",", ":"))
