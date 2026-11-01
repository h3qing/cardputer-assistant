"""Claude Code hook input -> one compact control request -> HookEvent (#33).

Hook stdin is untrusted JSON that can be megabytes (prompt, tool_input, last_assistant_message).
`cpa hook` reads at most STDIN_MAX bytes and forwards only an allowlist: the event, the full session id,
the notification type, the tool name, one detail (SessionStart source, SessionEnd reason, StopFailure
error), whether a subagent sent it, and a short clean excerpt (Notification message or Stop's
last_assistant_message). Prompts, tool input, paths and transcripts never leave the hook process.
The daemon re-checks every field with parse(); nothing from a hook is ever executed.
"""

from __future__ import annotations

import re
import unicodedata
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.protocol.validate import utf8_len

STDIN_MAX = 1 << 20
MSG_MAX = 120
SCAN_MAX = 2000
EVENTS = frozenset({"SessionStart", "UserPromptSubmit", "PreToolUse", "PermissionRequest", "PostToolUse",
                    "Notification", "Stop", "StopFailure", "SubagentStop", "PreCompact", "SessionEnd"})  # fmt: skip
SESSION_ID = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\Z")
TOOL = re.compile(r"[A-Za-z0-9_.:-]{1,64}\Z")
WORD = re.compile(r"[a-z0-9_]{1,32}\Z")
DETAIL_FIELD = {"SessionStart": "source", "SessionEnd": "reason", "StopFailure": "error"}
MSG_FIELD = {"Notification": "message", "Stop": "last_assistant_message"}
TOOL_EVENTS = frozenset({"PreToolUse", "PermissionRequest", "PostToolUse"})
MARKUP = re.compile(r"[`*]+")
HEADING = re.compile(r"(?:^|(?<= ))#{1,6} ")
KEYS = frozenset({"cmd", "ev", "sid", "ntype", "tool", "detail", "sub", "msg"})


@dataclass(frozen=True, slots=True)
class HookEvent:
    ev: str
    session_id: str
    ntype: str | None = None
    tool: str | None = None
    detail: str | None = None
    sub: bool = False
    msg: str | None = None


def _visible(ch: str) -> str:
    """Spaces for separators and controls; nothing for format, private and astral characters (no glyph on the
    device, and bidi overrides could disguise the text)."""
    category = unicodedata.category(ch)
    if category.startswith("Z") or category == "Cc":
        return " "
    return "" if category.startswith("C") or ord(ch) > 0xFFFF else ch


def excerpt(text: str, max_bytes: int = MSG_MAX) -> str | None:
    """One clean line of at most max_bytes UTF-8 bytes (… marks a cut), or None when nothing is left."""
    flat = " ".join(MARKUP.sub("", "".join(map(_visible, text[:SCAN_MAX]))).split())
    flat = HEADING.sub("", flat).strip()
    if utf8_len(flat) > max_bytes:
        flat = flat.encode()[: max_bytes - 3].decode("utf-8", "ignore").rstrip() + "…"
    return flat or None


def _word(value: Any, pattern: re.Pattern[str]) -> str | None:
    return value if isinstance(value, str) and pattern.match(value) else None


def _known(event: Any) -> bool:
    """A string check first: a list or object is unhashable and must not reach the set lookup."""
    return isinstance(event, str) and event in EVENTS


def _fields(doc: Mapping[str, Any], event: str) -> dict[str, Any]:
    message = doc.get(MSG_FIELD[event]) if event in MSG_FIELD else None
    agent = doc.get("agent_id")
    fields = {
        "ntype": _word(doc.get("notification_type"), WORD) if event == "Notification" else None,
        "tool": _word(doc.get("tool_name"), TOOL) if event in TOOL_EVENTS else None,
        "detail": _word(doc.get(DETAIL_FIELD[event]), WORD) if event in DETAIL_FIELD else None,
        "sub": True if isinstance(agent, str) and agent else None,
        "msg": excerpt(message) if isinstance(message, str) else None,
    }
    return {k: v for k, v in fields.items() if v is not None}


def compact(doc: Any, argv_event: str | None = None) -> dict[str, Any] | None:
    """The control request for one hook input, or None when the input is not something cpad tracks."""
    if not isinstance(doc, Mapping):
        return None
    event, session_id = doc.get("hook_event_name"), doc.get("session_id")
    if not _known(event) or (argv_event is not None and argv_event != event):
        return None
    if not isinstance(session_id, str) or not SESSION_ID.match(session_id):
        return None
    return {"cmd": "hook", "ev": event, "sid": session_id, **_fields(doc, event)}


def _optional(doc: Mapping[str, Any], key: str, pattern: re.Pattern[str]) -> str | None:
    value = doc.get(key)
    if value is not None and _word(value, pattern) is None:
        raise ValueError(f"hook {key} is not allowed")
    return value


def _message(value: Any) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str) or utf8_len(value) > MSG_MAX or excerpt(value) != value:
        raise ValueError("hook msg is not a clean excerpt")
    return value


def parse(doc: Mapping[str, Any]) -> HookEvent:
    """Daemon side: re-check a compact request field by field. Raises ValueError."""
    if set(doc) - KEYS:
        raise ValueError("hook request has unknown fields")
    if not _known(doc.get("ev")):
        raise ValueError("hook ev is not allowed")
    if not isinstance(doc.get("sid"), str) or not SESSION_ID.match(doc["sid"]):
        raise ValueError("hook sid must be a full session id")
    sub = doc.get("sub", False)
    if not isinstance(sub, bool):
        raise ValueError("hook sub must be true or false")
    return HookEvent(ev=doc["ev"], session_id=doc["sid"], ntype=_optional(doc, "ntype", WORD),
                     tool=_optional(doc, "tool", TOOL), detail=_optional(doc, "detail", WORD), sub=sub,
                     msg=_message(doc.get("msg")))  # fmt: skip
