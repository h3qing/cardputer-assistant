"""Expected values of a run (pure): the v0.2 hello fields (09 §6), the --expect-launcher judgement,
and exactly-once delivery of a set of typed notes.

A hello is either the wire dict the tap recorded or the daemon's DeviceHello; a v0.1 device sends
neither `store` nor `launcher`, which reads as None.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from harness.notes import NoteLine


def _field(hello: Any, name: str) -> Any:
    if isinstance(hello, Mapping):
        return hello.get(name)
    return getattr(hello, name, None)


def store_of(hello: Any) -> str | None:
    """hello.store: "sd" / "int" / "none", None from a v0.1 device."""
    return _field(hello, "store")


def launcher_of(hello: Any) -> bool | None:
    return _field(hello, "launcher")


def launcher_mismatch(hello: Any, expect: bool) -> str | None:
    """With --expect-launcher the device must say it runs under the Launcher; otherwise anything goes."""
    if not expect or launcher_of(hello) is True:
        return None
    got = launcher_of(hello)
    return f"--expect-launcher, but the device hello has launcher={got!r} (not installed from the Launcher?)"


@dataclass(frozen=True, slots=True)
class Delivery:
    counts: dict[str, int]  # expected text -> notes with exactly that text
    ids: tuple[str, ...]  # the item id of each expected text's first note, in text order
    missing: tuple[str, ...]
    doubled: tuple[str, ...]
    extra_ids: tuple[str, ...]  # received ids that belong to none of the expected notes
    received_twice: tuple[str, ...]

    @property
    def ok(self) -> bool:
        return not (self.missing or self.doubled or self.extra_ids or self.received_twice)


def _where(counts: Mapping[str, int], keep) -> tuple[str, ...]:
    return tuple(key for key, n in counts.items() if keep(n))


def delivery(texts: Sequence[str], notes: Sequence[NoteLine], received: Sequence[str]) -> Delivery:
    """texts typed on the device; notes in notes_dir; received: item.received ids of the same window."""
    typed = set(texts)
    wanted = [n for n in notes if n.text in typed]
    by_text = Counter(n.text for n in wanted)
    counts = {t: by_text[t] for t in texts}
    first = {n.text: n.item_id for n in reversed(wanted)}
    expected = {n.item_id for n in wanted}
    return Delivery(
        counts=counts,
        ids=tuple(first[t] for t in texts if t in first),
        missing=_where(counts, lambda n: n == 0),
        doubled=_where(counts, lambda n: n > 1),
        extra_ids=tuple(i for i in dict.fromkeys(received) if i not in expected),
        received_twice=_where(Counter(received), lambda n: n > 1),
    )
