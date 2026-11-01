"""Input and output JSON Schemas of the cpa mcp tools (docs/15 §3.4, §3.6). Static constants only.

Inputs are closed objects (additionalProperties false) with bounded fields; args.py validates calls
against these same schemas, so the published contract and the checks cannot drift apart. Outputs
list the fields we send; every output carries trust = "untrusted-user-data".
list_unfiled and get_filing_item (docs/23 §3.5) send the documents of the filing contract protocol/filing/v1
(ls, show) plus trust. Their output schemas are inlined, not a $ref into protocol/: a client compiles outputSchema
by itself and could not follow one. tests/contract/test_filing_contract.py pins them to the contract.
"""

from __future__ import annotations

from typing import Any

from cpad.filing.query import LINKS, MODES, TXS

TRUST = "untrusted-user-data"
DATE_PATTERN = r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$"
SID_PATTERN = r"^[0-9A-Za-z]{7}-[0-9A-Za-z]{5}-[0-9A-Za-z]{4}$"
PROJECT_PATTERN = r"^[a-z0-9][a-z0-9-]{0,11}$"
CURSOR_PATTERN = r"^[A-Za-z0-9_-]{1,512}$"
DEVICE_PATTERN = r"^[0-9a-f]{12}$"
SEARCH_SCOPES = ("notes", "summaries", "transcripts")
STATES = ("done", "transcript", "processing", "waiting", "blocked", "failed", "pulling")  # meanings: catalog.py
FILINGS = ("new", "filed", "handed", "done")  # the filing states MCP returns: never hidden (docs/23 §3.5)
PROJECTS_MAX = 9  # slots 1-9, one project each

Schema = dict[str, Any]


def _str(pattern: str, description: str) -> Schema:
    return {"type": "string", "pattern": pattern, "description": description}


def _int(lo: int, hi: int, default: int, description: str) -> Schema:
    return {"type": "integer", "minimum": lo, "maximum": hi, "default": default, "description": description}


def _enum(values: tuple[str, ...], description: str) -> Schema:
    return {"type": "string", "enum": list(values), "description": description}


def _flag(description: str) -> Schema:
    return {"type": "boolean", "default": False, "description": description}


def _some(item: Schema, max_items: int, description: str) -> Schema:
    return {"type": "array", "items": item, "uniqueItems": True, "maxItems": max_items, "description": description}


def closed(properties: dict[str, Schema], required: tuple[str, ...] = ()) -> Schema:
    return {"type": "object", "properties": properties, "required": list(required), "additionalProperties": False}


def output(properties: dict[str, Schema], required: tuple[str, ...]) -> Schema:
    props = {"trust": {"type": "string", "const": TRUST}, **properties}
    return {"type": "object", "properties": props, "required": ["trust", *required]}


DATE = _str(DATE_PATTERN, "A date YYYY-MM-DD in this Mac's local time")
FROM = _str(DATE_PATTERN, "First day to include, YYYY-MM-DD")
TO = _str(DATE_PATTERN, "Last day to include, YYYY-MM-DD")
SID = _str(SID_PATTERN, "Recording session id, e.g. 3QZK1M7-00042-0A1C (case does not matter)")
PROJECT = _str(PROJECT_PATTERN, "A registered project key")
CURSOR = _str(CURSOR_PATTERN, "next_cursor from the previous page, passed back unchanged")
OFFSET = _int(0, 10_000_000, 0, "Where to continue: next_offset from the previous call")

STRING = {"type": "string"}
NULL_STRING = {"type": ["string", "null"]}
INTEGER = {"type": "integer"}
NULL_INTEGER = {"type": ["integer", "null"]}
NUMBER = {"type": ["number", "null"]}
BOOLEAN = {"type": "boolean"}
OBJECT = {"type": "object"}
NULL_OBJECT = {"type": ["object", "null"]}


def _array(item: Schema) -> Schema:
    return {"type": "array", "items": item}


def _items(name: str, item: Schema, scanned: bool = False) -> Schema:
    extra = {"partial": BOOLEAN} if scanned else {}
    return output({name: _array(item), "next_cursor": STRING, "truncated": BOOLEAN, **extra}, (name,))


INPUTS: dict[str, Schema] = {
    "list_days": closed({"from": FROM, "to": TO, "limit": _int(1, 100, 31, "Days per page"), "cursor": CURSOR}),
    "read_daily_note": closed({"date": DATE, "offset": OFFSET}, ("date",)),
    "list_recordings": closed(
        {
            "from": FROM,
            "to": TO,
            "mode": _enum(("memo", "meeting"), "Only memos or only meetings"),
            "project": PROJECT,
            "state": _enum(STATES, "Only recordings in this state (meanings in the tool description)"),
            "limit": _int(1, 50, 20, "Recordings per page"),
            "cursor": CURSOR,
        }
    ),  # fmt: skip
    "get_recording": closed({"id": SID}, ("id",)),
    "read_transcript": closed({"id": SID, "offset": OFFSET}, ("id",)),
    "list_todos": closed(
        {
            "status": _enum(("open", "done", "all"), "Which tasks; default open"),
            "from": FROM,
            "to": TO,
            "project": PROJECT,
            "limit": _int(1, 100, 50, "Tasks per page"),
            "cursor": CURSOR,
        }
    ),  # fmt: skip
    "list_captures": closed(
        {
            "from": FROM,
            "to": TO,
            "kind": _enum(("note", "queue", "memo"), "Only this kind of capture"),
            "project": PROJECT,
            "limit": _int(1, 100, 50, "Captures per page"),
            "cursor": CURSOR,
        }
    ),  # fmt: skip
    "list_queue": closed({"project": PROJECT, "limit": _int(1, 100, 50, "Items to return")}),
    "get_status": closed({}),
    "search": closed(
        {
            "query": {"type": "string", "minLength": 1, "maxLength": 200, "description": "Literal text, any case"},
            "in": {
                "type": "array",
                "items": {"type": "string", "enum": list(SEARCH_SCOPES)},
                "uniqueItems": True,
                "maxItems": 3,
                "description": "Where to look; default everywhere",
            },
            "from": FROM,
            "to": TO,
            "limit": _int(1, 50, 20, "Hits to return"),
            "cursor": CURSOR,
        },
        ("query",),
    ),  # fmt: skip
    "queue_add": closed(
        {
            "project": PROJECT,
            "text": {"type": "string", "minLength": 1, "maxLength": 500, "description": "1 to 500 bytes of UTF-8"},
        },
        ("project", "text"),
    ),  # fmt: skip
    "list_unfiled": closed(
        {
            "filing": {**_enum((*FILINGS, "all"), "Which rows (meanings in the tool description)"), "default": "new"},
            "orphans": _flag("Also the new rows whose project key is another Mac's (asked)"),
            "from": FROM,
            "to": TO,
            "device": _str(DEVICE_PATTERN, "Only this device: the 12 hex digit id of a row"),
            "link": _enum(LINKS, "here = recorded while linked to this Mac; unknown = not known"),
            "mode": _enum(MODES, "Only memos or only meetings"),
            "project": _some(PROJECT, PROJECTS_MAX, "Only rows filed to any of these registered project keys"),
            "none": _flag("Only rows without a project"),
            "tx": _some(
                {"type": "string", "enum": list(TXS)},
                len(TXS),
                "Only rows in any of these transcription states: list_recordings' states, plus stale = no "
                "result 24 h after archiving",
            ),
            "query": {
                "type": "string",
                "minLength": 1,
                "maxLength": 200,
                "description": "Literal text, any case, in the title, the memo text or the transcript",
            },
            "limit": _int(1, 50, 20, "Rows per page"),
            "cursor": CURSOR,
        }
    ),  # fmt: skip
    "get_filing_item": closed({"id": SID}, ("id",)),
}

STATE = {"type": "string", "enum": list(STATES)}
NULL_STATE = {"type": ["string", "null"], "enum": [*STATES, None]}
_ENV = {"type": ["object", "null"], "properties": {"host": NULL_STRING, "link": NULL_STRING}}
_RECORDING_ROW = {"type": "object", "properties": {
    "id": STRING, "date": STRING, "started": NULL_STRING, "mode": NULL_STRING, "project": NULL_STRING,
    "duration_s": NUMBER, "state": STATE, "title": NULL_STRING, "has_summary": BOOLEAN,
    "has_transcript": BOOLEAN, "todos_n": NULL_INTEGER, "env": _ENV}}  # fmt: skip
_TODO = {"type": "object", "properties": {
    "text": STRING, "done": BOOLEAN, "date": NULL_STRING, "project": NULL_STRING, "source": OBJECT,
    "block": NULL_STRING}}  # fmt: skip
_CAPTURE = {
    "type": "object",
    "properties": {
        "id": STRING,
        "kind": STRING,
        "at": NULL_STRING,
        "project": NULL_STRING,
        "dest": NULL_STRING,
        "text": STRING,
    },
}
_QUEUE_ITEM = {
    "type": "object",
    "properties": {
        "qid": STRING,
        "project": STRING,
        "intent": STRING,
        "text": STRING,
        "origin": STRING,
        "uncertain": BOOLEAN,
    },
}
_HIT_SOURCE = {"type": "object", "properties": {"kind": {"type": "string", "enum": list(SEARCH_SCOPES)},
                                               "date": STRING, "id": STRING, "matches": INTEGER}}  # fmt: skip
_HIT = {"type": "object", "properties": {"source": _HIT_SOURCE, "line": NULL_INTEGER, "offset": INTEGER, "t_s": NUMBER,
                                         "i": INTEGER, "snippet": STRING}}  # fmt: skip
_SUMMARY = {"type": "object", "properties": {
    "text": STRING, "truncated": BOOLEAN,
    "error": {"type": "string", "enum": ["E_TOO_LARGE", "E_UNSAFE", "E_IO", "E_NOT_FOUND"]}}}  # fmt: skip
_SENTENCE = {"type": "object", "properties": {"i": INTEGER, "t_s": NUMBER, "text": STRING}}
_RESERVED = {"type": "null", "description": "Reserved (prof: docs/18 v0.8; clips: #52); null here"}
_FILING_ROW_PROPERTIES = {
    "id": STRING, "kind": STRING, "filing": {"type": "string", "enum": list(FILINGS)}, "why": _array(STRING),
    "asked": NULL_STRING,
    "at": {"type": "object", "properties": {"wall_ms": NULL_INTEGER, "tz_min": NULL_INTEGER, "uncertain": BOOLEAN}},
    "dur_ms": NULL_INTEGER, "device": NULL_STRING, "env": _ENV, "prof": _RESERVED, "project": NULL_STRING,
    "starred": BOOLEAN, "tx": STRING, "title": NULL_STRING, "text": NULL_STRING, "intent": NULL_STRING,
    "clips": _RESERVED,
    "queue": {"type": ["object", "null"], "properties": {"qid": STRING, "intent": STRING, "open": BOOLEAN}},
}  # fmt: skip
_FILING_ROW = {"type": "object", "properties": _FILING_ROW_PROPERTIES, "required": list(_FILING_ROW_PROPERTIES)}
_COUNTS = {"type": "object", "properties": {"new": INTEGER, "other_mac": INTEGER, "clips_waiting": INTEGER,
                                            "on_device": NULL_INTEGER}}  # fmt: skip
_FILING_TODO = {"type": "object", "properties": {"n": INTEGER, "text": STRING, "owner": NULL_STRING, "due": NULL_STRING,
                                                 "t_s": NUMBER, "confident": BOOLEAN}}  # fmt: skip
VERSION = {"type": "integer", "const": 1}

OUTPUTS: dict[str, Schema] = {
    "list_days": _items(
        "days", {"type": "object", "properties": {"date": STRING, "bytes": NULL_INTEGER, "recordings": NULL_INTEGER}}
    ),  # fmt: skip
    "read_daily_note": output(
        {"date": STRING, "text": STRING, "total_lines": INTEGER, "next_offset": INTEGER, "truncated": BOOLEAN},
        ("date", "text"),
    ),  # fmt: skip
    "list_recordings": _items("recordings", _RECORDING_ROW, scanned=True),
    "get_recording": output(
        {"id": STRING, "state": NULL_STATE, "meta": OBJECT, "result": OBJECT, "summary": _SUMMARY}, ("id",)
    ),  # fmt: skip
    "read_transcript": output(
        {"id": STRING, "sentences": _array(_SENTENCE), "total": INTEGER, "next_offset": INTEGER, "truncated": BOOLEAN},
        ("id", "sentences"),
    ),  # fmt: skip
    "list_todos": _items("todos", _TODO, scanned=True),
    "list_captures": _items("captures", _CAPTURE),
    "list_queue": _items("items", _QUEUE_ITEM),
    "get_status": output(
        {
            "daemon": STRING,
            "version": NULL_STRING,
            "link": NULL_STRING,
            "device": NULL_OBJECT,
            "agent": NULL_OBJECT,
            "projects": _array(OBJECT),
            "queue_open": NULL_INTEGER,
            "recordings": NULL_OBJECT,
        },
        ("daemon",),
    ),  # fmt: skip
    "search": output(
        {"hits": _array(_HIT), "partial": BOOLEAN, "scanned": INTEGER, "next_cursor": STRING, "truncated": BOOLEAN},
        ("hits", "partial"),
    ),  # fmt: skip
    "queue_add": output({"qid": STRING, "project": STRING, "warning": STRING}, ("qid", "project")),
    "list_unfiled": output(
        {
            "v": VERSION,
            "counts": _COUNTS,
            "next_cursor": NULL_STRING,
            "rows": _array(_FILING_ROW),
            "truncated": BOOLEAN,
        },
        ("v", "counts", "next_cursor", "rows"),
    ),  # fmt: skip
    "get_filing_item": output(
        {
            "v": VERSION,
            "row": _FILING_ROW,
            "text": NULL_STRING,
            "sentences": {
                "type": ["array", "null"],
                "items": {"type": "object", "properties": {"t_s": NUMBER, "text": STRING}},
            },
            "todos": _array(_FILING_TODO),
            "audio": _array(STRING),
            "fw": {"type": "null"},
            "queue_text": NULL_STRING,
            "next_offset": INTEGER,
            "truncated": BOOLEAN,
        },
        ("v", "row", "text", "sentences", "todos", "audio", "fw", "queue_text"),
    ),  # fmt: skip
}
