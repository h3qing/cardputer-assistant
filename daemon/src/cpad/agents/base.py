"""Agent adapter interface (05 §7.4). v0.1 has one implementation: agents/claude_code."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True, slots=True)
class AgentCaps:
    present: bool
    version: str
    spawn: bool
    reply: bool


@dataclass(frozen=True, slots=True)
class SessionInfo:
    """One session from `agents --json`; kind is tty|bg, status is busy|shell|idle|waiting|unknown.

    waiting_for is permission|question|dialog while status is waiting (from waitingFor, #5), else None.
    """

    session_id: str
    sid: str
    short: str | None
    kind: str
    status: str
    state: str | None
    name: str
    cwd: str | None
    pid: int | None = None
    waiting_for: str | None = None


@dataclass(frozen=True, slots=True)
class SpawnRequest:
    root: str
    name: str
    text: str
    permission_mode: str


@dataclass(frozen=True, slots=True)
class SpawnResult:
    ok: bool
    name: str | None = None
    short: str | None = None
    code: str | None = None
    why: str | None = None  # untrusted | login when the CLI said why the spawn failed (#25)


@dataclass(frozen=True, slots=True)
class ReplyRequest:
    sid: str
    text: str


@dataclass(frozen=True, slots=True)
class ReplyResult:
    ok: bool
    code: str | None = None

    @property
    def forked(self) -> bool:
        return self.code == "E_REPLY_FORKED"


class AgentAdapter(Protocol):
    name: str

    async def probe(self) -> AgentCaps: ...

    async def list_sessions(self, include_all: bool = False) -> tuple[SessionInfo, ...]: ...

    async def spawn(self, req: SpawnRequest) -> SpawnResult: ...

    async def reply(self, req: ReplyRequest) -> ReplyResult: ...
