"""Small statistics shared by the M3 gate runners (pure, no I/O)."""

from __future__ import annotations

import math

HOUR_BYTES = 115_200_000  # one hour of 16 kHz 16-bit mono (11 §4.4)


def percentile(values, pct):
    """Nearest-rank percentile; None for an empty list."""
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(pct / 100 * len(ordered)))
    return ordered[rank - 1]


def summarize(values):
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "p50": percentile(values, 50),
        "p95": percentile(values, 95),
        "max": max(values),
        "mean": round(sum(values) / len(values), 2),
    }


def kbs(nbytes, secs):
    """KB/s with KB = 1024 B (the unit of audio-capture §7.2); None without a span."""
    if not secs:
        return None
    return nbytes / 1024 / secs


def minutes_per_hour(kb_per_s):
    """Minutes to move one recorded hour at kb_per_s."""
    if not kb_per_s:
        return None
    return HOUR_BYTES / (kb_per_s * 1024) / 60
