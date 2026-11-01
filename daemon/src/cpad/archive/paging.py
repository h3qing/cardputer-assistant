"""Key-based pages over newest-first lists (docs/15 §3.4). Pure.

Every list is sorted by a string key, descending (newest first). A page holds the items strictly after
the previous page's last key, so a page stays correct when new items arrive or the last item vanishes.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass
from itertools import accumulate


@dataclass(frozen=True, slots=True)
class Page[T]:
    items: tuple[T, ...]
    next_key: str | None = None  # the key of the last item when more follow; for a partial page, where to resume
    partial: bool = False  # a scan cap was hit: the list may miss items (narrow the range)


def page[T](items: Iterable[T], key: Callable[[T], str], after: str | None, limit: int) -> Page[T]:
    rest = [item for item in items if after is None or key(item) < after]
    chosen = tuple(rest[:limit])
    more = len(rest) > len(chosen) and bool(chosen)
    return Page(chosen, key(chosen[-1]) if more else None)


BUDGET = 20 * 1024  # default text budget in UTF-8 bytes; `cpa mcp` passes what is left of its 24 KiB


def clip(text: str, max_bytes: int) -> tuple[str, bool]:
    """(text cut to at most max_bytes of UTF-8 on a character boundary, whether it was cut)."""
    raw = text.encode("utf-8")
    if len(raw) <= max_bytes:
        return text, False
    return raw[: max(0, max_bytes)].decode("utf-8", errors="ignore"), True


def take(texts: Sequence[str], offset: int, budget: int, overhead: int) -> tuple[tuple[str, ...], int, bool]:
    """(texts from `offset` that fit in `budget` bytes, each costing its UTF-8 length + overhead; the offset after
    them; whether anything was left out or cut). One text longer than the budget is cut, so a page always moves on."""
    if offset < 0:
        raise ValueError("offset must not be negative")
    rest = texts[offset:]
    fits = sum(1 for total in accumulate(len(t.encode("utf-8")) + overhead for t in rest) if total <= budget)
    if fits == 0 and rest:
        return (clip(rest[0], budget - overhead)[0],), offset + 1, True
    return tuple(rest[:fits]), offset + fits, offset + fits < len(texts)
