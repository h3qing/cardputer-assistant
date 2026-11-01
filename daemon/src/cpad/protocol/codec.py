"""Decode and encode protocol v0 lines (05 §6.1, §6.2; decode order as in protocol/README.md).

decode_line() runs the receiver pipeline in the contract's fixed order and returns a verdict:
  1. drop every \\r; more than line_max bytes (incl. \\n)   -> E_TOO_LARGE
  2. does not start with '{'                               -> noise
  3. not a JSON object                                      -> E_PARSE
  4. v missing / not an integer -> E_BAD_FIELD v; v != 0 -> E_VERSION; t / id / re malformed -> E_BAD_FIELD
  5. unknown t: E_UNKNOWN_TYPE, or ignored when it is a reply
  6. invalid UTF-8 anywhere, then the type's rules          -> E_BAD_FIELD <path>
reply_id is the id the receiver must answer exactly once; None when no reply is due.
"""

from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.protocol.limits import LIMITS
from cpad.protocol.specs import SPECS
from cpad.protocol.validate import Path, format_path, is_int, matches

KNOWN_TYPES = frozenset(SPECS)
REPLYABLE = frozenset({"ok", "E_VERSION", "E_UNKNOWN_TYPE", "E_BAD_FIELD"})
LINE_MAX: int = LIMITS["line_max"]


class LineTooLarge(ValueError):
    """An encoded line would exceed line_max bytes."""


@dataclass(frozen=True, slots=True)
class Decoded:
    outcome: str
    fields: tuple[str, ...] = ()
    msg: Mapping[str, Any] | None = None
    reply_id: str | None = None

    @property
    def ok(self) -> bool:
        return self.outcome == "ok"


def _reject_constant(name: str) -> None:
    raise ValueError(f"not JSON: {name}")


def _parse(body: bytes) -> dict | None:
    try:
        obj = json.loads(body.decode("utf-8", "surrogateescape"), parse_constant=_reject_constant)
    except (ValueError, RecursionError):
        return None
    return obj if isinstance(obj, dict) else None


def _envelope(obj: Mapping[str, Any]) -> tuple[str, tuple[str, ...]] | None:
    if not is_int(obj.get("v")):
        return "E_BAD_FIELD", ("v",)
    if obj["v"] != LIMITS["protocol_version"]:
        return "E_VERSION", ()
    for name, pattern in (("t", "type_pattern"), ("id", "id_pattern")):
        if not matches(LIMITS[pattern], obj.get(name)):
            return "E_BAD_FIELD", (name,)
    if "re" in obj and not matches(LIMITS["id_pattern"], obj["re"]):
        return "E_BAD_FIELD", ("re",)
    return None


def _has_surrogate(text: str) -> bool:
    return any("\ud800" <= ch <= "\udfff" for ch in text)


def _bad_utf8(value: Any, path: Path = ()) -> list[str]:
    if isinstance(value, str):
        return [format_path(path)] if _has_surrogate(value) else []
    if isinstance(value, dict):
        keys = [format_path((*path, k)) for k in value if _has_surrogate(k)]
        return keys + [f for k, v in value.items() for f in _bad_utf8(v, (*path, k))]
    if isinstance(value, list):
        return [f for i, v in enumerate(value) for f in _bad_utf8(v, (*path, i))]
    return []


def _verdict(obj: Mapping[str, Any]) -> tuple[str, tuple[str, ...]]:
    envelope = _envelope(obj)
    if envelope:
        return envelope
    if obj["t"] not in SPECS:
        return ("ignored", ()) if "re" in obj else ("E_UNKNOWN_TYPE", ())
    broken = _bad_utf8(obj)
    if broken:
        return "E_BAD_FIELD", tuple(sorted(set(broken)))
    faults = SPECS[obj["t"]](obj, ())
    fields = tuple(sorted({format_path(p) for p in faults}))
    return ("E_BAD_FIELD", fields) if fields else ("ok", ())


def _reply_id(obj: Mapping[str, Any], outcome: str) -> str | None:
    answerable = outcome in REPLYABLE and "re" not in obj
    return obj["id"] if answerable and matches(LIMITS["id_pattern"], obj.get("id")) else None


def decode_line(raw: bytes) -> Decoded:
    line = raw.replace(b"\r", b"")
    if len(line) > LINE_MAX:
        return Decoded("E_TOO_LARGE")
    body = line[:-1] if line.endswith(b"\n") else line
    if not body.startswith(b"{"):
        return Decoded("noise")
    obj = _parse(body)
    if obj is None:
        return Decoded("E_PARSE")
    outcome, fields = _verdict(obj)
    return Decoded(outcome, fields, obj, _reply_id(obj, outcome))


def serialize(msg: Mapping[str, Any]) -> bytes:
    text = json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"
    return text.encode("utf-8")


def encode(msg: Mapping[str, Any]) -> bytes:
    """One canonical line (05 §6.1); raises LineTooLarge above line_max bytes."""
    line = serialize(msg)
    if len(line) > LINE_MAX:
        raise LineTooLarge(f"{msg.get('t')} line is {len(line)} bytes")
    return line


def fit_list(msg: Mapping[str, Any], key: str) -> tuple[dict[str, Any], int]:
    """Drop entries from the tail of msg[key] until the line fits (05 §6.1). Returns (msg, kept)."""
    items = list(msg[key])
    lo, hi = 0, len(items)
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if len(serialize({**msg, key: items[:mid]})) <= LINE_MAX:
            lo = mid
        else:
            hi = mid - 1
    return {**msg, key: items[:lo]}, lo
