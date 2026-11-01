"""Percentiles by the nearest-rank method (ADR-0001 §2: with 10 samples p95 is the maximum)."""

from __future__ import annotations

import math
from collections.abc import Sequence


def percentile(values: Sequence[float], q: float) -> float:
    """The smallest sample with at least q % of the samples at or below it."""
    if not 0 < q <= 100:
        raise ValueError("q must be in (0, 100]")
    if not values:
        raise ValueError("no samples")
    ordered = sorted(values)
    rank = math.ceil(q / 100 * len(ordered))
    return ordered[rank - 1]


def summary(values: Sequence[float], digits: int = 3) -> dict[str, float | int]:
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "min": round(min(values), digits),
        "p50": round(percentile(values, 50), digits),
        "p95": round(percentile(values, 95), digits),
        "max": round(max(values), digits),
    }
