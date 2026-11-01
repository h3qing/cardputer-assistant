"""ClaudeCodeAdapter (05 §7.4, §7.5) with the CLI facts from docs/gates/cli-gates.md.

spawn: [bin, --bg, --name, <key>/<slug>, --permission-mode, <mode>, --, <text>] in cwd=root with
env=spawn_env. Exit code 0 is not success (G8): the short id comes from stdout line 1, or from a
before/after listing diff by name, and `agents --json --all` must not show the session failed/crashed.
reply (only when experimental_bg_reply): re-list, require a unique idle bg session for the sid,
`stop <short>`, then `--bg --resume <sessionId> -- <text>` (no other flags, G3), then check for a fork:
"started a copy" on stderr, another id on stdout, or a new sessionId with the same name in `--all`. A fork disables
reply for the rest of this run.
blockers (#25): a failed spawn says why (untrusted folder, logged out); while `auth status` says logged out, a
spawn re-checks and is refused without starting a session that could only answer "Login expired".
"""

from __future__ import annotations

import logging
import os
from collections.abc import Awaitable, Callable, Iterable, Mapping
from dataclasses import replace

from cpad.agents.base import AgentCaps, ReplyRequest, ReplyResult, SessionInfo, SpawnRequest, SpawnResult
from cpad.agents.claude_code import argv as cli
from cpad.agents.claude_code import blockers
from cpad.agents.claude_code.parse import AgentError, parse_sessions, parse_short, parse_version
from cpad.agents.claude_code.process import ProcResult, run
from cpad.logging.redact import redact_argv, redact_stderr
from cpad.projects.registry import Project

log = logging.getLogger("cpad.agents")
Runner = Callable[..., Awaitable[ProcResult]]
FAILED_STATES = frozenset({"failed", "crashed"})
FORK_HINT = "started a copy"
LIST_TIMEOUT_S = 10.0
SPAWN_TIMEOUT_S = 20.0
ENV_WHITELIST = ("PATH", "HOME", "LANG", "SHELL", "USER")


def default_env() -> dict[str, str]:
    """Whitelisted variables of this process, used when no spawn_env was captured at install time."""
    return {k: v for k, v in os.environ.items() if k in ENV_WHITELIST or k.startswith("LC_")}


class ClaudeCodeAdapter:
    name = "claude_code"

    def __init__(self, claude_bin: str, spawn_env: Mapping[str, str], *, runner: Runner = run, reply_enabled=False):
        self._bin = claude_bin
        self._env = dict(spawn_env) or default_env()
        self._run = runner
        self._reply_enabled = reply_enabled
        self._forked = False
        self._blockers = blockers.Blockers()

    @property
    def reply_available(self) -> bool:
        return self._reply_enabled and not self._forked

    @property
    def logged_in(self) -> bool | None:
        return self._blockers.logged_in

    def warnings(self, projects: Iterable[Project]) -> tuple[str, ...]:
        return blockers.warnings(self._blockers, projects)

    async def check_login(self) -> bool | None:
        """`claude auth status` in spawn_env, read-only. Its stdout names the account, so it is never logged."""
        result = await self._run(cli.auth_status_argv(self._bin), cwd=None, env=self._env, timeout=LIST_TIMEOUT_S)
        logged_in = None if result.rc is None else blockers.parse_logged_in(result.stdout)
        self._blockers = replace(self._blockers, logged_in=logged_in)
        return logged_in

    async def _call(self, args: list[str], timeout: float, cwd: str | None = None, secret: str = "") -> ProcResult:
        result = await self._run(args, cwd=cwd, env=self._env, timeout=timeout)
        if result.rc != 0:
            fields = {"argv": redact_argv(args), "rc": result.rc, "timed_out": result.timed_out,
                      "stderr": redact_stderr(result.stderr, (secret,))}  # fmt: skip
            log.warning("claude command failed", extra={"fields": fields})
        return result

    async def probe(self) -> AgentCaps:
        result = await self._call(cli.version_argv(self._bin), LIST_TIMEOUT_S)
        present = result.rc == 0
        version = parse_version(result.stdout) if present else ""
        return AgentCaps(present, version, present, present and self.reply_available)

    async def list_sessions(self, include_all: bool = False) -> tuple[SessionInfo, ...]:
        result = await self._call(cli.list_argv(self._bin, include_all), LIST_TIMEOUT_S)
        if result.rc != 0:
            raise AgentError("agents --json failed")
        return parse_sessions(result.stdout)

    async def _listing(self, include_all: bool = False) -> tuple[SessionInfo, ...]:
        try:
            return await self.list_sessions(include_all)
        except AgentError:
            return ()

    async def spawn(self, req: SpawnRequest) -> SpawnResult:
        if cli.check_text(req.text):
            return SpawnResult(False, code="E_BAD_FIELD")
        try:
            args = cli.spawn_argv(self._bin, req.name, req.permission_mode, req.text)
        except ValueError:
            return SpawnResult(False, code="E_SPAWN")
        if self._blockers.logged_in is False and await self.check_login() is False:
            return SpawnResult(False, code="E_SPAWN", why=blockers.LOGIN)
        before = {s.session_id for s in await self._listing()}
        result = await self._call(args, SPAWN_TIMEOUT_S, cwd=req.root, secret=req.text)
        if result.rc != 0:
            return self._settled(req.root, SpawnResult(False, code="E_SPAWN", why=blockers.spawn_why(result.stderr)))
        short = parse_short(result.stdout) or await self._find_new(before, req.name)
        if short is None or await self._crashed(short):
            return SpawnResult(False, name=req.name, code="E_SPAWN")
        return self._settled(req.root, SpawnResult(True, name=req.name, short=short))

    def _settled(self, root: str, result: SpawnResult) -> SpawnResult:
        self._blockers = blockers.after_spawn(self._blockers, root, ok=result.ok, why=result.why)
        return result

    async def _find_new(self, before: set[str], name: str) -> str | None:
        fresh = [s for s in await self._listing() if s.session_id not in before and s.name == name]
        return fresh[0].short or fresh[0].sid if len(fresh) == 1 else None

    async def _crashed(self, short: str) -> bool:
        return any(s.short == short and s.state in FAILED_STATES for s in await self._listing(include_all=True))

    async def reply(self, req: ReplyRequest) -> ReplyResult:
        if not self.reply_available:
            return ReplyResult(False, "E_NOT_REPLYABLE")
        if cli.check_text(req.text):
            return ReplyResult(False, "E_BAD_FIELD")
        matches = [s for s in await self._listing() if s.sid == req.sid]
        if len(matches) != 1:
            return ReplyResult(False, "E_UNKNOWN_SESSION")
        target = matches[0]
        if target.kind != "bg" or target.status != "idle" or not target.short:
            return ReplyResult(False, "E_NOT_REPLYABLE")
        return await self._stop_and_resume(target, req.text)

    async def _stop_and_resume(self, target: SessionInfo, text: str) -> ReplyResult:
        before = {s.session_id for s in await self._listing(include_all=True)}
        if (await self._call(cli.stop_argv(self._bin, target.short or ""), LIST_TIMEOUT_S)).rc != 0:
            return ReplyResult(False, "E_INTERNAL")
        resumed = await self._call(cli.resume_argv(self._bin, target.session_id, text), SPAWN_TIMEOUT_S, secret=text)
        if resumed.rc != 0:
            return ReplyResult(False, "E_INTERNAL")
        copies = [
            s for s in await self._listing(include_all=True) if s.session_id not in before and s.name == target.name
        ]
        if FORK_HINT in resumed.stderr or parse_short(resumed.stdout) not in (None, target.short) or copies:
            self._forked = True
            log.warning("reply forked the session; reply disabled for this run", extra={"fields": {"sid": target.sid}})
            return ReplyResult(False, "E_REPLY_FORKED")
        return ReplyResult(True)
