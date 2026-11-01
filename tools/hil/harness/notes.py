"""Reading the daily notes cpad writes: `- HH:MM #<key> <text> ^cpa-<monoid lower>` (05 §7.8)."""

from __future__ import annotations

import re
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

LINE = re.compile(r"^- (~?\d\d:\d\d) (?:#(\S+) )?(.*) \^cpa-([0-9a-z]{7}-[0-9a-z]{5}-[0-9a-z]{4})$")
DAILY = re.compile(r"^\d{4}-\d{2}-\d{2}\.md$")


@dataclass(frozen=True, slots=True)
class NoteLine:
    time: str
    tag: str | None
    text: str
    item_id: str  # the MonoId, upper case as on the wire


def parse_line(line: str) -> NoteLine | None:
    m = LINE.match(line.rstrip("\n"))
    return NoteLine(m.group(1), m.group(2), m.group(3), m.group(4).upper()) if m else None


def read_notes(notes_dir: Path) -> tuple[NoteLine, ...]:
    if not notes_dir.is_dir():
        return ()
    files = sorted(p for p in notes_dir.iterdir() if DAILY.match(p.name))
    lines = (line for f in files for line in f.read_text(encoding="utf-8").splitlines())
    return tuple(n for n in map(parse_line, lines) if n is not None)


def count_text(notes: Sequence[NoteLine], text: str) -> int:
    return sum(1 for n in notes if n.text == text)


def count_block(notes: Sequence[NoteLine], item_id: str) -> int:
    return sum(1 for n in notes if n.item_id == item_id.upper())
