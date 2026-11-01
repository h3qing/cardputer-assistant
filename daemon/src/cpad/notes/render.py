"""Daily note lines (05 §7.8, US-12): `- HH:MM #<key> <text> ^cpa-<monoid lower>`.

Time comes from the device wall clock when known (and representable), otherwise from received_at
with a `~` prefix.
Unassigned and orphan notes carry no tag. Control characters become spaces, so one note is one line.
v0.4.1 (docs/13 §9.3, §11.1): a note with dest todo is a checkbox, `- [ ] HH:MM ...`, the same format as a
recording todo (recordings/route.py). No dest, daily or a dest this cpad does not know: the v0.3 line.
"""

from __future__ import annotations

import re
from collections.abc import Mapping
from datetime import datetime, tzinfo
from typing import Any

CONTROL = re.compile(r"[\u0000-\u001f\u007f]")
TODO = "todo"
KNOWN_DESTS = frozenset({"daily", TODO})


def clean(text: str) -> str:
    return CONTROL.sub(" ", text)


def block_id(item_id: str) -> str:
    return f"^cpa-{item_id.lower()}"


def _when(record: Mapping[str, Any], tz: tzinfo | None) -> tuple[datetime, str]:
    wall_ms = (record.get("at_dev") or {}).get("wall_ms")
    if isinstance(wall_ms, int):
        try:
            return datetime.fromtimestamp(wall_ms / 1000, tz), ""
        except (ValueError, OverflowError, OSError):
            pass  # protocol-valid up to 2^53-1, but past year 9999: treat the device clock as unknown
    return datetime.fromisoformat(record["received_at"]).astimezone(tz), "~"


def note_entry(record: Mapping[str, Any], tz: tzinfo | None = None) -> tuple[str, str] | None:
    """(file name, line) for an item.received note; None for anything else."""
    if record.get("ev") != "item.received" or record.get("kind") != "note":
        return None
    body = record.get("body") or {}
    when, mark = _when(record, tz)
    project = None if record.get("orphan") else body.get("project")
    tag = f"#{project} " if project else ""
    box = "[ ] " if body.get("dest") == TODO else ""
    line = f"- {box}{mark}{when:%H:%M} {tag}{clean(str(body.get('text', '')))} {block_id(record['id'])}"
    return f"{when:%Y-%m-%d}.md", line
