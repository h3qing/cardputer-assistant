"""Output budget and rate limits (docs/15 §3.8). Pure.

A tool's structured JSON stays within OUTPUT_MAX bytes of UTF-8; lists are cut short and say so.
Two token buckets per process: reads 60 per minute (burst 20), writes 10 per minute (burst 3).
"""

from __future__ import annotations

import json
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from typing import Any

OUTPUT_MAX = 24 * 1024
RESERVE = 192  # room for next_cursor / next_offset / truncated added after fit()


def dumps(doc: Any) -> str:
    return json.dumps(doc, ensure_ascii=False, separators=(",", ":"))


def json_size(doc: Any) -> int:
    return len(dumps(doc).encode("utf-8", "surrogatepass"))


def fit(
    items: Iterable[Mapping[str, Any]], base: Mapping[str, Any], key: str, budget: int = OUTPUT_MAX
) -> tuple[tuple[Mapping[str, Any], ...], bool]:
    """The longest prefix of items that fits in budget next to base (minus RESERVE); and whether it was cut."""
    size = json_size({**base, key: []}) + RESERVE
    kept: list[Mapping[str, Any]] = []
    for item in items:
        size += json_size(item) + (1 if kept else 0)
        if size > budget:
            return tuple(kept), True
        kept.append(item)
    return tuple(kept), False


def clip(text: str | None, max_chars: int) -> str | None:
    if text is None or len(text) <= max_chars:
        return text
    return text[: max_chars - 1] + "…"


@dataclass(frozen=True, slots=True)
class Rate:
    per_min: int
    burst: int

    @property
    def per_s(self) -> float:
        return self.per_min / 60.0


READ = Rate(per_min=60, burst=20)
WRITE = Rate(per_min=10, burst=3)


@dataclass(frozen=True, slots=True)
class Bucket:
    tokens: float
    at: float


def full(rate: Rate, now: float) -> Bucket:
    return Bucket(float(rate.burst), now)


def take(bucket: Bucket, now: float, rate: Rate) -> tuple[Bucket, bool]:
    elapsed = max(0.0, now - bucket.at)
    tokens = min(float(rate.burst), bucket.tokens + elapsed * rate.per_s)
    if tokens >= 1.0:
        return Bucket(tokens - 1.0, now), True
    return Bucket(tokens, now), False


def retry_after(bucket: Bucket, rate: Rate) -> float:
    return round(max(0.0, 1.0 - bucket.tokens) / rate.per_s, 1)
