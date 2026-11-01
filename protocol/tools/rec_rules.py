"""Contract rules of the v0.4.1 additions (docs/13 §12.2): the recordings app's requests and capture destinations.

REQUIRED_REC_CASES  invalid cases the vectors must carry, so both codecs are seen to reject them (V41-P);
LARGE_HOST_REPLIES  host replies allowed above host_reply_max. rec.items answers rec.list, which shares the one
                    device in-flight slot of queue.done / inbox.dismiss (docs/13 §10.2), so the device RX bound
                    becomes host_inflight_max + 5 * host_reply_max + line_max = 7424 B <= device_rx_buffer;
APP_REQUESTS        device requests only a device app sends (the link cores alone never do), so a flow that
                    carries them lives in flows/app/.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping
from typing import Any

APP_REQUESTS = frozenset({"rec.list", "rec.act"})
LARGE_HOST_REPLIES = frozenset({"rec.items"})


def _first(msg: Mapping[str, Any]) -> Mapping[str, Any]:
    items = msg.get("items")
    return items[0] if isinstance(items, list) and items and isinstance(items[0], dict) else {}


def _utf8_len(value: Any) -> int:
    return len(value.encode("utf-8")) if isinstance(value, str) else -1


def _has_upper(value: Any) -> bool:
    return isinstance(value, str) and any(c.isupper() for c in value)


# (type, field, label, predicate on msg), checked like check_vectors.REQUIRED_FILE_CASES.
REQUIRED_REC_CASES: tuple[tuple[str, str, str, Callable[[Mapping[str, Any]], bool]], ...] = (
    ("rec.list", "max", "max 7", lambda m: m.get("max") == 7),
    ("rec.act", "op", "op 'delete'", lambda m: m.get("op") == "delete"),
    ("rec.act", "op", "op 'trash'", lambda m: m.get("op") == "trash"),
    ("rec.list", "cursor", "33-character cursor", lambda m: _utf8_len(m.get("cursor")) == 33),
    ("rec.list", "cursor", "cursor containing '/'", lambda m: "/" in str(m.get("cursor") or "")),
    ("rec.items", "items[0].title", "49-byte title", lambda m: _utf8_len(_first(m).get("title")) == 49),
    ("rec.items", "items", "7 items", lambda m: len(m.get("items") or ()) == 7),
    ("rec.act", "project", "tag with an invalid project key", lambda m: m.get("op") == "tag" and "project" in m),
    ("outbox.item", "body.dest", "dest 'Todo'", lambda m: (m.get("body") or {}).get("dest") == "Todo"),
    ("file.items", "items[0].dest", "dest 'Todo'", lambda m: _first(m).get("dest") == "Todo"),
    ("hello", "host_id", "upper-case host_id", lambda m: _has_upper(m.get("host_id"))),
)


def sends_app_requests(flow: Any) -> bool:
    steps = flow.get("steps", ()) if isinstance(flow, dict) else ()
    return any(
        isinstance(s, dict) and s.get("from") == "device" and (s.get("msg") or {}).get("t") in APP_REQUESTS
        for s in steps
    )
