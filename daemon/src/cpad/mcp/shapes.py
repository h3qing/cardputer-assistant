"""What a tool may return, field by field (docs/15 §3.4). Pure.

Rows from the archive reader are reduced to the published output fields, nested objects to their
own allow-lists, and long strings are clipped, so one row can never fill the output budget alone.
The filing documents (list_unfiled, get_filing_item; docs/23 §3.5) keep every field of the contract
protocol/filing/v1, already bounded there, and nothing else: env only host and link (never a network),
no firmware, and prof and clips null until a later version says what they may hold.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from cpad.mcp.limits import clip

FIELDS: Mapping[str, tuple[str, ...]] = {
    "days": ("date", "bytes", "recordings"),
    "recordings": (
        "id",
        "date",
        "started",
        "mode",
        "project",
        "duration_s",
        "state",
        "title",
        "has_summary",
        "has_transcript",
        "todos_n",
        "env",
    ),  # fmt: skip
    "todos": ("text", "done", "date", "project", "source", "block"),
    "captures": ("id", "kind", "at", "project", "dest", "text"),
    "items": ("qid", "project", "intent", "text", "origin", "uncertain"),
    "hits": ("source", "line", "offset", "t_s", "i", "snippet"),
    "sentences": ("i", "t_s", "text"),
}
NESTED: Mapping[str, tuple[str, ...]] = {
    "source": ("kind", "date", "id", "line", "matches"),
    "summary": ("text", "truncated", "error"),
    "meta": ("mode", "started", "duration_s", "project", "segments", "marks", "consent", "env"),
    "result": ("state", "level", "title", "memo", "actions", "error_code", "error"),
    "project": ("key", "name"),
    "started": ("wall_ms", "tz_min", "uncertain"),
    "segments": ("i", "t0_ms"),
    "marks": ("t_s", "k"),
    "memo": ("text", "intent"),
    "actions": ("n", "text", "owner", "due", "t_s", "confident"),
    "error": ("code", "retryable"),
    "env": ("host", "link"),
}
CLIPS: Mapping[str, int] = {"title": 200, "text": 2000, "snippet": 300, "block": 64, "memo": 2000}
SCALAR = (str, int, float, bool, type(None))
FILING_ROW = ("id", "kind", "filing", "why", "asked", "at", "dur_ms", "device", "env", "prof", "project", "starred",
              "tx", "title", "text", "intent", "clips", "queue")  # fmt: skip
FILING_NESTED: Mapping[str, tuple[str, ...]] = {
    "at": ("wall_ms", "tz_min", "uncertain"),
    "env": ("host", "link"),
    "queue": ("qid", "intent", "open"),
}
COUNTS = ("new", "other_mac", "clips_waiting", "on_device")
SHOW_TODO = ("n", "text", "owner", "due", "t_s", "confident")


def _value(key: str, value: Any, depth: int = 0) -> Any:
    if isinstance(value, str):
        return clip(value, CLIPS.get(key, 200))
    if isinstance(value, SCALAR):
        return value
    if isinstance(value, Mapping) and key in NESTED:
        return pick_keys(value, NESTED[key], depth + 1)
    if isinstance(value, list | tuple) and depth < 2:
        return [_value(key, v, depth + 1) for v in value[:50] if isinstance(v, (*SCALAR, Mapping))]
    return None


def pick_keys(row: Mapping[str, Any], keys: tuple[str, ...], depth: int = 0) -> dict[str, Any]:
    return {k: _value(k, row[k], depth) for k in keys if k in row}


def pick(kind: str, row: Mapping[str, Any]) -> dict[str, Any]:
    return pick_keys(row, FIELDS[kind])


def _typed(value: Any, kind: type | tuple[type, ...]) -> Any:
    return value if isinstance(value, kind) and not (kind is int and isinstance(value, bool)) else None


def status(doc: Mapping[str, Any]) -> dict[str, Any]:
    """get_status, typed as published (the archive already reduced it to an allow-list)."""
    projects = doc.get("projects") if isinstance(doc.get("projects"), list) else []
    return {
        "daemon": _typed(doc.get("daemon"), str) or "not running",
        "version": _typed(doc.get("version"), str),
        "link": _typed(doc.get("link"), str),
        "device": _typed(doc.get("device"), dict),
        "agent": _typed(doc.get("agent"), dict),
        "projects": [p for p in projects if isinstance(p, dict)],
        "queue_open": _typed(doc.get("queue_open"), int),
        "recordings": _typed(doc.get("recordings"), dict),
    }


def detail(doc: Mapping[str, Any]) -> dict[str, Any]:
    """get_recording: id and state, plus the meta / result / summary objects, each reduced to its allow-list."""
    out: dict[str, Any] = {"id": str(doc.get("id", "")), "state": _typed(doc.get("state"), str)}
    for key in ("meta", "result", "summary"):
        value = doc.get(key)
        out[key] = pick_keys(value, NESTED[key]) if isinstance(value, Mapping) else {}
    if isinstance(doc.get("summary"), Mapping) and isinstance(doc["summary"].get("text"), str):
        out["summary"]["text"] = doc["summary"]["text"]  # already cut to the budget by the reader
    return out


def _only(value: Any, keys: tuple[str, ...]) -> dict[str, Any] | None:
    return {k: value.get(k) for k in keys} if isinstance(value, Mapping) else None


def filing_row(row: Mapping[str, Any]) -> dict[str, Any]:
    """A row of list_unfiled, and get_filing_item's row."""
    flat = {k: row.get(k) for k in FILING_ROW}
    return {**flat, **{k: _only(row.get(k), keys) for k, keys in FILING_NESTED.items()}, "prof": None, "clips": None}


def filing_page(doc: Mapping[str, Any]) -> dict[str, Any]:
    """list_unfiled: `cpa filing ls --json`'s page."""
    return {"v": doc.get("v"), "counts": _only(doc.get("counts"), COUNTS), "next_cursor": doc.get("next_cursor"),
            "rows": [filing_row(r) for r in doc.get("rows", ())]}  # fmt: skip


def filing_item(doc: Mapping[str, Any]) -> dict[str, Any]:
    """get_filing_item: `cpa filing show --json`'s document, without the firmware (fw null)."""
    lines = doc.get("sentences")
    return {"v": doc.get("v"), "row": filing_row(doc.get("row") or {}), "text": doc.get("text"),
            "sentences": None if lines is None else [_only(s, ("t_s", "text")) for s in lines],
            "todos": [_only(t, SHOW_TODO) for t in doc.get("todos", ())], "audio": list(doc.get("audio", ())),
            "fw": None, "queue_text": doc.get("queue_text")}  # fmt: skip
