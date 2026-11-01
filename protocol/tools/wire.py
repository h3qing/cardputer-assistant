"""Reference decoder for one protocol v0 line (docs/05-architecture.md §6.1, §6.2).

decode_line() runs the receiver pipeline in a fixed order and returns the verdict:
  1. drop every \\r; more than line_max bytes (incl. \\n)  -> E_TOO_LARGE
  2. does not start with '{'                              -> noise (dropped, counted)
  3. not a JSON object                                     -> E_PARSE
  4. v missing / not an integer                            -> E_BAD_FIELD v
     v != 0                                                -> E_VERSION
     t, id, re malformed                                   -> E_BAD_FIELD t|id|re
  5. unknown t: E_UNKNOWN_TYPE, or ignored when it is a reply (has re)
  6. invalid UTF-8 anywhere, then the type's schema        -> E_BAD_FIELD <path>
reply_id is the id the receiver must answer (exactly once); None when no reply is due.
"""

from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import dataclass
from itertools import chain
from typing import Any

from schemas import Contract, ecma_search, field_errors, format_path

REPLYABLE = frozenset({"ok", "E_VERSION", "E_UNKNOWN_TYPE", "E_BAD_FIELD"})


@dataclass(frozen=True)
class Decoded:
    outcome: str
    fields: tuple[str, ...] = ()
    msg: Mapping[str, Any] | None = None
    reply_id: str | None = None


def canonical_line(msg: Mapping[str, Any]) -> bytes:
    text = json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n"
    return text.encode("utf-8", "surrogatepass")


def _reject_constant(name: str) -> None:
    raise ValueError(f"not JSON: {name}")


def _parse(body: bytes) -> Any:
    try:
        obj = json.loads(body.decode("utf-8", "surrogateescape"), parse_constant=_reject_constant)
    except ValueError:
        return None
    return obj if isinstance(obj, dict) else None


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _matches(value: Any, pattern: str) -> bool:
    return isinstance(value, str) and ecma_search(pattern, value)


def _envelope(contract: Contract, obj: Mapping[str, Any]) -> tuple[str, tuple[str, ...]] | None:
    limits = contract.limits
    if not _is_int(obj.get("v")):
        return "E_BAD_FIELD", ("v",)
    if obj["v"] != limits["protocol_version"]:
        return "E_VERSION", ()
    for name, pattern in (("t", "type_pattern"), ("id", "id_pattern")):
        if not _matches(obj.get(name), limits[pattern]):
            return "E_BAD_FIELD", (name,)
    if "re" in obj and not _matches(obj["re"], limits["id_pattern"]):
        return "E_BAD_FIELD", ("re",)
    return None


def _has_surrogate(text: str) -> bool:
    return any("\ud800" <= ch <= "\udfff" for ch in text)


def _bad_utf8(value: Any, path: tuple = ()) -> tuple[str, ...]:
    if isinstance(value, str):
        return (format_path(path),) if _has_surrogate(value) else ()
    if isinstance(value, dict):
        keys = tuple(format_path((*path, k)) for k in value if _has_surrogate(k))
        return keys + tuple(chain.from_iterable(_bad_utf8(v, (*path, k)) for k, v in value.items()))
    if isinstance(value, list):
        return tuple(chain.from_iterable(_bad_utf8(v, (*path, i)) for i, v in enumerate(value)))
    return ()


def _verdict(contract: Contract, obj: Mapping[str, Any]) -> tuple[str, tuple[str, ...]]:
    envelope = _envelope(contract, obj)
    if envelope:
        return envelope
    if obj["t"] not in contract.schemas:
        return ("ignored", ()) if "re" in obj else ("E_UNKNOWN_TYPE", ())
    broken = _bad_utf8(obj)
    if broken:
        return "E_BAD_FIELD", tuple(sorted(set(broken)))
    fields = field_errors(contract, obj)
    return ("E_BAD_FIELD", fields) if fields else ("ok", ())


def _reply_id(contract: Contract, obj: Mapping[str, Any], outcome: str) -> str | None:
    answerable = outcome in REPLYABLE and "re" not in obj
    return obj["id"] if answerable and _matches(obj.get("id"), contract.limits["id_pattern"]) else None


def decode_line(contract: Contract, raw: bytes) -> Decoded:
    line = raw.replace(b"\r", b"")
    if len(line) > contract.limits["line_max"]:
        return Decoded("E_TOO_LARGE")
    body = line[:-1] if line.endswith(b"\n") else line
    if not body.startswith(b"{"):
        return Decoded("noise")
    obj = _parse(body)
    if obj is None:
        return Decoded("E_PARSE")
    outcome, fields = _verdict(contract, obj)
    return Decoded(outcome, fields, obj, _reply_id(contract, obj, outcome))
