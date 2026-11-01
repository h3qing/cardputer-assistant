"""Captures from the records that already carry the text (docs/14 §5.1, §6.2, §6.3). Pure.

item.received (a typed note) and rec.result (a voice memo) are the only places the text lives; the
dest.* records never repeat it. A record without a dest, or with `daily`, is not a capture: it takes
the v0.3 path unchanged and writes no dest.* record at all. `how` defaults to `key` (v0.4.1 sends a
dest only when a key chose it); an unknown value counts as `app`, the strictest. Meetings ignore dest.
"""

from __future__ import annotations

from collections.abc import Mapping
from datetime import datetime, tzinfo
from typing import Any

from cpad.dest.model import DAILY, HOWS, VOICE_TEXT_MAX, Capture, How


def stamp(wall_ms: int | None, received_at: str, tz: tzinfo | None = None) -> str:
    """'YYYY-MM-DD HH:MM' from the device clock, else from received_at with a '~' prefix (as notes.render)."""
    if isinstance(wall_ms, int) and not isinstance(wall_ms, bool):
        try:
            return f"{datetime.fromtimestamp(wall_ms / 1000, tz):%Y-%m-%d %H:%M}"
        except (ValueError, OverflowError, OSError):
            pass  # protocol-valid up to 2^53-1, but past year 9999: treat the device clock as unknown
    return f"~{datetime.fromisoformat(received_at).astimezone(tz):%Y-%m-%d %H:%M}"


def _how(value: Any) -> How:
    if value is None:
        return "key"
    return value if value in HOWS else "app"


def _dest(value: Any) -> str | None:
    return value if isinstance(value, str) and value != DAILY else None


MALFORMED = (ValueError, KeyError, TypeError, AttributeError)


def _str(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def _int(value: Any) -> int | None:
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _from_item(record: Mapping[str, Any], tz: tzinfo | None) -> Capture | None:
    body = record.get("body") or {}
    dest = _dest(body.get("dest"))
    if dest is None or not isinstance(record["id"], str):
        return None
    wall_ms = (record.get("at_dev") or {}).get("wall_ms")
    return Capture(
        ref=record["id"],
        source="typed",
        dest=dest,
        how=_how(body.get("how")),
        project=None if record.get("orphan") else _str(body.get("project")),
        text=_str(body.get("text")),
        stamp=stamp(wall_ms, record["received_at"], tz),
        prof=_str(record.get("prof")),
    )


def capture_from_item(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None:
    """item.received of kind note whose body.dest is set and not daily; else (or when malformed) None."""
    if record.get("ev") != "item.received" or record.get("kind") != "note":
        return None
    try:
        return _from_item(record, tz)
    except MALFORMED:
        return None


def _from_result(record: Mapping[str, Any], tz: tzinfo | None) -> Capture | None:
    dest = _dest(record.get("dest"))
    if dest is None or not isinstance(record["rec"], str):
        return None
    when = stamp(record.get("started_ms"), record["at"], tz)
    text = _str(record.get("memo_text"))
    return Capture(
        ref=record["rec"],
        source="voice",
        dest=dest,
        how=_how(record.get("how")),
        project=_str(record.get("project")),
        text=text[:VOICE_TEXT_MAX] if text is not None else None,
        stamp=f"~{when}" if record.get("uncertain") and not when.startswith("~") else when,
        prof=_str(record.get("prof")),
        dur_ms=_int(record.get("dur_ms")),
    )


def capture_from_result(record: Mapping[str, Any], tz: tzinfo | None = None) -> Capture | None:
    """rec.result of a finished memo whose dest is set and not daily; text = memo_text[:VOICE_TEXT_MAX]."""
    if record.get("ev") != "rec.result" or record.get("mode") != "memo" or record.get("state") != "done":
        return None
    try:
        return _from_result(record, tz)
    except MALFORMED:
        return None
