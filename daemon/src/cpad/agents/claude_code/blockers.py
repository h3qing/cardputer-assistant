"""Why a spawn cannot work yet, and the one-time fix (#25; Claude Code 2.1.306 strings).

`claude --bg` in a folder the CLI has not trusted exits 1 with "Workspace not trusted. ..." on stderr (the
Claude desktop app keeps its own trust records, so a folder trusted there may not be trusted for the CLI). A
logged-out CLI says "Not logged in" / "Login expired", and `claude auth status` prints {"loggedIn": false, ...}.
cpad never accepts trust or logs in for the user: it reports the reason (agent.result `why`, events.jsonl,
`cpa status`) with the exact command to run. Inputs are bounded; nothing in them is executed.
"""

from __future__ import annotations

import json
import shlex
from collections.abc import Iterable
from dataclasses import dataclass, replace

from cpad.projects.registry import Project

UNTRUSTED = "untrusted"
LOGIN = "login"
WHYS = (UNTRUSTED, LOGIN)
TRUST_PREFIX = "Workspace not trusted."
LOGIN_HINTS = ("Not logged in", "Login expired", "Please run /login", "claude auth login")
STDERR_SCAN_MAX = 4096
AUTH_STDOUT_MAX = 65536
LOGIN_FIX = (
    "claude is not logged in for cpad (claude auth status: loggedIn false), so spawning is off. "
    "One-time fix: run `claude auth login` in a terminal (or /login inside claude), then spawn again."
)


@dataclass(frozen=True, slots=True)
class Blockers:
    """Project roots whose last spawn failed as untrusted, and the last known login state (None = unknown)."""

    untrusted: frozenset[str] = frozenset()
    logged_in: bool | None = None


def spawn_why(stderr: str) -> str | None:
    """untrusted / login when the stderr of a failed `claude --bg` says so, else None."""
    head = stderr[:STDERR_SCAN_MAX]
    if any(line.strip().startswith(TRUST_PREFIX) for line in head.splitlines()):
        return UNTRUSTED
    return LOGIN if any(hint in head for hint in LOGIN_HINTS) else None


def parse_logged_in(stdout: str) -> bool | None:
    """loggedIn from `claude auth status` (JSON by default); None when the answer is missing or unclear."""
    if len(stdout) > AUTH_STDOUT_MAX:
        return None
    try:
        doc = json.loads(stdout)
    except ValueError:
        return None
    value = doc.get("loggedIn") if isinstance(doc, dict) else None
    return value if isinstance(value, bool) else None


def after_spawn(state: Blockers, root: str, *, ok: bool, why: str | None) -> Blockers:
    if ok:
        return replace(state, untrusted=state.untrusted - {root})
    if why == UNTRUSTED:
        return replace(state, untrusted=state.untrusted | {root})
    return replace(state, logged_in=False) if why == LOGIN else state


def _trust_fix(project: Project) -> str:
    return (
        f"project {project.key}: the last spawn failed because Claude Code has not trusted {project.root}. "
        f"One-time fix: run `cd {shlex.quote(project.root)} && claude` in a terminal, accept the trust prompt, "
        "then spawn again (cpad never accepts it for you)."
    )


def warnings(state: Blockers, projects: Iterable[Project]) -> tuple[str, ...]:
    login = (LOGIN_FIX,) if state.logged_in is False else ()
    return (*login, *(_trust_fix(p) for p in projects if p.root in state.untrusted))
