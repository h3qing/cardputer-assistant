"""Effects returned by hub.reduce(); the shell (hub.runner) executes them strictly in order."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.agents.base import ReplyRequest, SpawnRequest


@dataclass(frozen=True, slots=True)
class Send:
    """A reply to a device request (not subject to credit)."""

    msg: Any


@dataclass(frozen=True, slots=True)
class Push:
    """A host-initiated snapshot (sent under the credit window)."""

    msg: Any


@dataclass(frozen=True, slots=True)
class Append:
    """Append to events.jsonl and fsync; queue.added records get their qid at write time."""

    records: tuple[Mapping[str, Any], ...]


@dataclass(frozen=True, slots=True)
class WriteNote:
    record: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class RunSpawn:
    re: str
    request: SpawnRequest
    project: str
    qid: str | None


@dataclass(frozen=True, slots=True)
class RunReply:
    re: str
    request: ReplyRequest
