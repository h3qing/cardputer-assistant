"""Parse Claude Code 2.1.272 output (05 §7.5; docs/gates/cli-gates.md §2, §5).

agents --json is parsed leniently: kind interactive -> tty, background (or registry form bg) -> bg,
daemon / daemon-worker / anything else is skipped; an unknown or missing status becomes unknown.
The short id is only read from the first stdout line of `claude --bg`: ^backgrounded · <8 hex>.
"""

from __future__ import annotations

import json
import re
from collections.abc import Mapping
from typing import Any

from cpad.agents.base import SessionInfo

KINDS = {"interactive": "tty", "tty": "tty", "background": "bg", "bg": "bg"}
STATUSES = frozenset({"busy", "shell", "idle", "waiting"})
# waitingFor (2.1.306, only while status is waiting) grouped the way the CLI's agent view groups it (#5, G4);
# any other value is still a wait for the user, reason unknown.
WAITING_FOR = {"permission prompt": "permission", "sandbox request": "permission", "worker request": "permission",
               "goal proposal": "permission", "input needed": "question"}  # fmt: skip
SHORT = re.compile(r"^backgrounded · ([0-9a-f]{8})\b")
SID = re.compile(r"[0-9a-f]{8}\Z")
SHORT_FIELD = re.compile(r"[!-~]{1,16}\Z")
VERSION = re.compile(r"\d+\.\d+\.\d+")


class AgentError(RuntimeError):
    """The CLI failed or printed something unparseable."""


def _text(value: Any) -> str | None:
    return value if isinstance(value, str) else None


def _session(item: Any) -> SessionInfo | None:
    if not isinstance(item, Mapping):
        return None
    kind = KINDS.get(item.get("kind"))
    session_id = _text(item.get("sessionId"))
    if kind is None or session_id is None or not SID.match(session_id[:8]):
        return None
    sid = session_id[:8]
    short = item.get("id") if isinstance(item.get("id"), str) and SHORT_FIELD.match(item["id"]) else sid
    status = item.get("status") if item.get("status") in STATUSES else "unknown"
    pid = item.get("pid") if isinstance(item.get("pid"), int) else None
    return SessionInfo(
        session_id=session_id,
        sid=sid,
        short=short if kind == "bg" else None,
        kind=kind,
        status=status,
        state=_text(item.get("state")),
        name=_text(item.get("name")) or "",
        cwd=_text(item.get("cwd")),
        pid=pid,
        waiting_for=WAITING_FOR.get(_text(item.get("waitingFor")) or "", "dialog") if status == "waiting" else None,
    )


def parse_sessions(text: str) -> tuple[SessionInfo, ...]:
    try:
        doc = json.loads(text)
    except ValueError as exc:
        raise AgentError("agents --json printed invalid JSON") from exc
    if isinstance(doc, Mapping):
        doc = doc.get("agents")
    if not isinstance(doc, list):
        raise AgentError("agents --json did not print a list")
    return tuple(s for s in (_session(item) for item in doc) if s is not None)


def parse_short(stdout: str) -> str | None:
    first = stdout.splitlines()[0] if stdout else ""
    match = SHORT.match(first)
    return match.group(1) if match else None


def parse_version(stdout: str) -> str:
    match = VERSION.search(stdout)
    return match.group(0) if match else stdout.strip()[:32]
