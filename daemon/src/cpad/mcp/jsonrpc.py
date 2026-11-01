"""Strict JSON-RPC 2.0 framing for the stdio transport (docs/15 §3.2, §3.3).

One message per line, at most LINE_MAX bytes of UTF-8. No batches. `id` is a string or an integer.
parse() never raises: it returns a Request, a Notification, a ready error reply, or None for a
message that gets no reply (a client response, or a notification we cannot read).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Any

LINE_MAX = 1 << 20

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603
UNSUPPORTED_PROTOCOL_VERSION = -32022


@dataclass(frozen=True, slots=True)
class Request:
    id: str | int
    method: str
    params: dict[str, Any] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class Notification:
    method: str
    params: dict[str, Any] = field(default_factory=dict)


Parsed = Request | Notification | dict[str, Any] | None


def result(msg_id: str | int, body: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": "2.0", "id": msg_id, "result": body}


def error(msg_id: str | int | None, code: int, message: str, data: Any = None) -> dict[str, Any]:
    body: dict[str, Any] = {"code": code, "message": message}
    if data is not None:
        body["data"] = data
    return {"jsonrpc": "2.0", "id": msg_id, "error": body}


def encode(msg: dict[str, Any]) -> bytes:
    """One line. Raw UTF-8, unless a client-sent lone surrogate forces \\u escapes."""
    try:
        return json.dumps(msg, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
    except UnicodeEncodeError:
        return json.dumps(msg, ensure_ascii=True, separators=(",", ":")).encode("ascii") + b"\n"


def _valid_id(value: Any) -> bool:
    return isinstance(value, str) or (isinstance(value, int) and not isinstance(value, bool))


def _decode(raw: bytes) -> Any:
    if len(raw) > LINE_MAX:
        raise OverflowError
    return json.loads(raw.decode("utf-8"))


def _message(doc: dict[str, Any]) -> Parsed:
    has_id = "id" in doc
    msg_id = doc.get("id") if has_id and _valid_id(doc.get("id")) else None
    if doc.get("jsonrpc") != "2.0" or (has_id and msg_id is None):
        return error(msg_id, INVALID_REQUEST, "Invalid request")
    method = doc.get("method")
    if not isinstance(method, str) or not method:
        return error(msg_id, INVALID_REQUEST, "Invalid request")
    params = doc.get("params", {})
    if not isinstance(params, dict):
        return error(msg_id, INVALID_PARAMS, "params must be an object") if has_id else None
    return Request(msg_id, method, params) if has_id else Notification(method, params)


def parse(raw: bytes) -> Parsed:
    try:
        doc = _decode(raw)
    except OverflowError:
        return error(None, INVALID_REQUEST, "Message too large")
    except (ValueError, RecursionError):
        return error(None, PARSE_ERROR, "Parse error")
    if isinstance(doc, list):
        return error(None, INVALID_REQUEST, "Batch requests are not supported")
    if not isinstance(doc, dict):
        return error(None, INVALID_REQUEST, "Invalid request")
    if "method" not in doc and ("result" in doc or "error" in doc):
        return None
    return _message(doc)
