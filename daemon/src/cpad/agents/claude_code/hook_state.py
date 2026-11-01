"""Per-session state from Claude Code hooks, merged with the agents --json listing (#33, #5).

The hook says why; the poll says whether it is still true. Phases: working, needs (why: permission, plan,
question, input, login), done (why: input after idle_prompt, error after StopFailure) and ended.
A hook decides a session's status while the poll agrees with it, and also while the poll has not yet
caught up (no poll taken after the hook has shown a matching status, for at most LAG_S). Once a poll has
matched, the next poll that disagrees wins: the user answered at the terminal, or a hook is not installed.
A dialog wait (permission, plan, question) gets only NEEDS_GRACE_S instead of LAG_S: most approvals end it
with no hook at all, so a poll that started that long after the hook wins even before one has matched.
An unknown poll status neither confirms nor contradicts, and holds a wait for at most LAG_S.
Events from subagents (agent_id set) never change the session. waiting_for (#5: permission, question or
dialog, set only while waiting) is kept true to the status merge() settles on. Pure and immutable.
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, replace
from types import MappingProxyType

from cpad.agents.base import SessionInfo
from cpad.agents.claude_code.hooks import HookEvent

LAG_S = 10.0
NEEDS_GRACE_S = 2.0
BOOK_MAX = 64
LOGIN_ERRORS = frozenset({"authentication_failed", "oauth_org_not_allowed", "cloud_credential_error"})
QUESTION_TOOLS = {"AskUserQuestion": "question", "ExitPlanMode": "plan"}
STATUS = {"working": "busy", "needs": "waiting", "done": "idle"}
EXACT = {"working": frozenset({"busy", "shell"}), "needs": frozenset({"waiting"}), "done": frozenset({"idle"})}
WAITING_FOR = {"permission": "permission", "plan": "permission", "question": "question", "input": "question"}
DIALOGS = frozenset({"permission", "plan", "question"})


@dataclass(frozen=True, slots=True)
class HookState:
    phase: str
    why: str | None = None
    msg: str | None = None
    tool: str | None = None
    at: float = 0.0
    seen: bool = False


@dataclass(frozen=True, slots=True)
class SessionView:
    """A listing row with its effective status, plus why it needs you and the latest excerpt (hooks only)."""

    info: SessionInfo
    why: str | None = None
    msg: str | None = None


Book = Mapping[str, HookState]
EMPTY: Book = MappingProxyType({})
Step = Callable[[HookState | None, HookEvent, float], HookState | None]


def _new(phase: str, why: str | None = None, msg: str | None = None, tool: str | None = None, at: float = 0.0):
    return HookState(phase, why, msg, tool, at)


def _tool(prev: HookState | None, ev: HookEvent, now: float) -> HookState | None:
    if ev.ev == "PermissionRequest":
        return _new("needs", QUESTION_TOOLS.get(ev.tool or "", "permission"), tool=ev.tool, at=now)
    if ev.ev == "PreToolUse" and ev.tool == "AskUserQuestion":
        return _new("needs", "question", tool=ev.tool, at=now)
    return _new("working", tool=ev.tool, at=now)


def _permission_prompt(prev: HookState | None, ev: HookEvent, now: float) -> HookState:
    """Fires ~6 s into any dialog: adds its message to a wait already known (a question or plan keeps its
    reason, and a poll that confirmed it still counts), or starts a permission wait."""
    if prev is not None and prev.phase == "needs":
        return replace(prev, why=prev.why if prev.why in ("question", "plan") else "permission", msg=ev.msg)
    return _new("needs", "permission", ev.msg, prev.tool if prev else None, now)


def _notification(prev: HookState | None, ev: HookEvent, now: float) -> HookState | None:
    waiting = prev is not None and prev.phase == "needs"
    if ev.ntype == "permission_prompt":
        return _permission_prompt(prev, ev, now)
    if ev.ntype in ("elicitation_dialog", "elicitation_url_dialog"):
        return _new("needs", "question", ev.msg, at=now)
    if ev.ntype == "quota_auto_resume_stale":
        return _new("needs", "input", ev.msg, at=now)
    if ev.ntype == "idle_prompt" and not waiting:
        return _new("done", "input", ev.msg, at=now)
    if ev.ntype == "auth_success" and prev is not None and prev.why == "login":
        return _new("done", at=now)
    return prev


def _stop_failure(prev: HookState | None, ev: HookEvent, now: float) -> HookState | None:
    return _new("needs", "login", at=now) if ev.detail in LOGIN_ERRORS else _new("done", "error", ev.detail, at=now)


def _session_start(prev: HookState | None, ev: HookEvent, now: float) -> HookState | None:
    return prev if ev.detail == "compact" else _new("done", at=now)


STEPS: dict[str, Step] = {
    "SessionStart": _session_start,
    "UserPromptSubmit": lambda prev, ev, now: _new("working", at=now),
    "PreCompact": lambda prev, ev, now: _new("working", at=now),
    "PreToolUse": _tool,
    "PermissionRequest": _tool,
    "PostToolUse": _tool,
    "Notification": _notification,
    "Stop": lambda prev, ev, now: _new("done", msg=ev.msg, at=now),
    "StopFailure": _stop_failure,
    "SessionEnd": lambda prev, ev, now: _new("ended", at=now),
}


def record(book: Book, ev: HookEvent, now: float) -> Book:
    """The book after one hook event; at most BOOK_MAX sessions, the most recent kept."""
    prev = book.get(ev.session_id)
    step = STEPS.get(ev.ev)
    new = prev if ev.sub or step is None else step(prev, ev, now)
    if new is None or new is prev:
        return book
    entries = sorted({**book, ev.session_id: new}.items(), key=lambda kv: kv[1].at, reverse=True)
    return MappingProxyType(dict(entries[:BOOK_MAX]))


def _compatible(state: HookState, status: str, polled_at: float) -> bool:
    if state.phase == "ended":
        return False
    if status == "unknown":
        return state.phase != "needs" or polled_at - state.at < LAG_S
    login = state.why == "login" and status == "idle"
    return status in EXACT[state.phase] or login


def observe(book: Book, listing: Iterable[SessionInfo], polled_at: float) -> Book:
    """Marks hooks a newer poll has confirmed; forgets unlisted sessions once LAG_S has passed."""
    rows = {s.session_id: s.status for s in listing}
    kept = {}
    for sid, state in book.items():
        if sid not in rows and polled_at - state.at > LAG_S:
            continue
        confirmed = sid in rows and polled_at > state.at and _compatible(state, rows[sid], polled_at)
        kept[sid] = replace(state, seen=True) if confirmed and not state.seen else state
    return MappingProxyType(kept)


def _lag_s(state: HookState) -> float:
    return NEEDS_GRACE_S if state.phase == "needs" and state.why in DIALOGS else LAG_S


def _hook_wins(info: SessionInfo, state: HookState | None, polled_at: float) -> bool:
    lagging = state is not None and not state.seen and polled_at - state.at < _lag_s(state)
    return state is not None and (_compatible(state, info.status, polled_at) or lagging)


def _view(info: SessionInfo, state: HookState | None, polled_at: float) -> SessionView | None:
    if state is None or not _hook_wins(info, state, polled_at):
        return SessionView(info, info.waiting_for)
    if state.phase == "ended":
        return None
    if info.status in EXACT[state.phase]:
        return SessionView(info, state.why, state.msg)
    status = STATUS[state.phase]
    waiting_for = WAITING_FOR.get(state.why or "", "dialog") if status == "waiting" else None
    return SessionView(replace(info, status=status, waiting_for=waiting_for), state.why, state.msg)


def merge(listing: Iterable[SessionInfo], book: Book, polled_at: float) -> tuple[SessionView, ...]:
    """Listing order kept; sessions a fresh SessionEnd hook ended are left out."""
    views = (_view(info, book.get(info.session_id), polled_at) for info in listing)
    return tuple(v for v in views if v is not None)


def merge_pairs(pairs: tuple, polled_at: float, book: Book) -> tuple:
    """merge() over (session, project key or None) pairs, as the poller resolves them."""
    project = {s.session_id: p for s, p in pairs}
    return tuple((v.info, project[v.info.session_id]) for v in merge((s for s, _ in pairs), book, polled_at))


def summary(book: Book, now: float) -> dict[str, object]:
    """For `cpa status --json`: are hooks arriving at all?"""
    last = max((s.at for s in book.values()), default=None)
    return {"sessions": len(book), "last_ago_s": None if last is None else round(now - last, 1)}
