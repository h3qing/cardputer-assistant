"""Ids of archive layout v1 (docs/15 §3.8): dates, session ids, recording directory names, opaque cursors. Pure.

Only values that passed these parsers ever become path components. Error messages name the field, never
the value (SECURITY.md: errors do not echo input).
"""

from __future__ import annotations

import base64
import binascii
import json
import re
from datetime import date

from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import matches

DATE_RE = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}\Z")
SID_PATTERN = LIMITS["monoid_pattern"]  # upper case Crockford base32; parse_sid folds case first
REC_DIR_RE = re.compile(r"([0-9]{4})-([0-9a-z-]{18})\Z")
CURSOR_MAX = 512
KEY_MAX = 256


class BadId(ValueError):
    """An id, date or cursor failed validation; `field` names the argument (never its value)."""

    def __init__(self, field: str) -> None:
        super().__init__(f"invalid {field}")
        self.field = field


def parse_date(text: object) -> date:
    """YYYY-MM-DD (ASCII digits) of a real calendar date."""
    if not isinstance(text, str) or not DATE_RE.match(text):
        raise BadId("date")
    try:
        return date.fromisoformat(text)
    except ValueError:
        raise BadId("date") from None


def is_date(text: object) -> bool:
    try:
        parse_date(text)
    except BadId:
        return False
    return True


def parse_sid(text: object) -> str:
    """A recording session id (MonoId), case-insensitive; returned in lower case like the directory names."""
    if not isinstance(text, str) or not text.isascii() or not matches(SID_PATTERN, text.upper()):
        raise BadId("id")
    return text.lower()


def split_rec_dir(name: str) -> tuple[str, str] | None:
    """(hhmm, sid) of a recording directory name `HHMM-<sid lower>`; None for anything else."""
    found = REC_DIR_RE.match(name)
    if found is None or not matches(SID_PATTERN, found.group(2).upper()):
        return None
    return found.group(1), found.group(2)


def in_range(day: str, start: date | None, end: date | None) -> bool:
    """Whether an ISO date string lies in [start, end]; either end may be open."""
    return (start is None or day >= start.isoformat()) and (end is None or day <= end.isoformat())


def encode_cursor(kind: str, key: str) -> str:
    raw = json.dumps([kind, key], ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")


def decode_cursor(kind: str, text: object) -> str:
    """The key inside a cursor made by encode_cursor for the same kind of list."""
    if not isinstance(text, str) or not text.isascii() or not 0 < len(text) <= CURSOR_MAX:
        raise BadId("cursor")
    try:
        doc = json.loads(base64.urlsafe_b64decode(text + "=" * (-len(text) % 4)))
    except (binascii.Error, ValueError, RecursionError):
        raise BadId("cursor") from None
    valid = isinstance(doc, list) and len(doc) == 2 and all(isinstance(x, str) for x in doc)
    if not valid or doc[0] != kind or not 0 < len(doc[1]) <= KEY_MAX:
        raise BadId("cursor")
    return doc[1]
