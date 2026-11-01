"""Tool arguments (docs/15 §3.4, §3.6, §3.8), checked against the tool's own published input schema.

The schema is turned into cpad.protocol.validate checks, so what clients are told and what we
enforce are the same table. Then a few meanings the schema cannot say: real calendar dates,
from <= to, lower-case session ids, and the device's text rules for anything written.
"""

from __future__ import annotations

from collections.abc import Mapping
from datetime import date
from types import MappingProxyType
from typing import Any

from cpad.mcp.schemas import INPUTS, SEARCH_SCOPES, Schema
from cpad.notes.render import clean
from cpad.protocol.validate import Check, array, boolean, integer, one_of, string, utf8_len

TEXT_MAX_BYTES = 500
DATE_FIELDS = ("from", "to", "date")


class BadArg(ValueError):
    """An argument failed validation; `field` names it (never its value)."""

    def __init__(self, field: str):
        super().__init__(field)
        self.field = field


def check_for(prop: Schema) -> Check:
    if "enum" in prop:
        return one_of(*prop["enum"])
    if prop["type"] == "string":
        return string(prop.get("pattern"), min_len=prop.get("minLength", 0), max_len=prop.get("maxLength"))
    if prop["type"] == "integer":
        return integer(prop["minimum"], prop["maximum"])
    if prop["type"] == "boolean":
        return boolean()
    return array(check_for(prop["items"]), max_items=prop.get("maxItems"), unique=prop.get("uniqueItems", False))


CHECKS: Mapping[str, Mapping[str, Check]] = MappingProxyType(
    {tool: MappingProxyType({k: check_for(p) for k, p in s["properties"].items()}) for tool, s in INPUTS.items()}
)


def _text(value: str) -> str:
    text = clean(value).strip()
    if not text or utf8_len(text) > TEXT_MAX_BYTES or text.startswith("-"):
        raise BadArg("text")
    return text


def _date(key: str, value: str) -> date:
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise BadArg(key) from exc


def _convert(key: str, value: Any) -> Any:
    if value is None:
        return None
    if key in DATE_FIELDS:
        return _date(key, value)
    if key == "id":
        return value.lower()
    if isinstance(value, list):
        return tuple(value)  # search's in, list_unfiled's project and tx
    return _text(value) if key == "text" else value


def _checked(name: str, raw: Any) -> dict[str, Any]:
    raw = {} if raw is None else raw
    if not isinstance(raw, dict) or any(k not in CHECKS[name] for k in raw):
        raise BadArg("arguments")
    missing = next((k for k in INPUTS[name]["required"] if k not in raw), None)
    if missing is not None:
        raise BadArg(missing)
    bad = next((k for k, v in raw.items() if CHECKS[name][k](v, ())), None)
    if bad is not None:
        raise BadArg(bad)
    return raw


def parse(name: str, raw: Any) -> Mapping[str, Any]:
    """Validated arguments with defaults filled in. KeyError for an unknown tool, BadArg for bad input."""
    props = INPUTS[name]["properties"]
    given = _checked(name, raw)
    defaults = {k: p.get("default") for k, p in props.items()}
    if "in" in props:
        defaults["in"] = SEARCH_SCOPES
    values = {k: _convert(k, v) for k, v in {**defaults, **given}.items()}
    if values.get("from") and values.get("to") and values["from"] > values["to"]:
        raise BadArg("from")
    return MappingProxyType(values)
