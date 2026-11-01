"""Inputs to hub.reduce(). `now` is monotonic seconds (since / age), `wall` is epoch seconds (records)."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Any

from cpad.agents.base import ReplyResult, SessionInfo, SpawnResult
from cpad.projects.registry import Project


@dataclass(frozen=True, slots=True)
class DeviceRequest:
    """A valid device request (OutboxItem, AgentSpawn, AgentReply, QueueDone, InboxDismiss)."""

    msg: Any
    now: float
    wall: float
    duplicate: bool = False


@dataclass(frozen=True, slots=True)
class Appended:
    """Records now in events.jsonl (ours with qids assigned, or appended by `cpa queue ...`)."""

    records: tuple[Mapping[str, Any], ...]
    now: float


@dataclass(frozen=True, slots=True)
class Polled:
    sessions: tuple[tuple[SessionInfo, str | None], ...]
    now: float


@dataclass(frozen=True, slots=True)
class ProjectsLoaded:
    projects: tuple[Project, ...]
    now: float


@dataclass(frozen=True, slots=True)
class AgentProbed:
    present: bool
    version: str
    errored: bool = False
    reply: bool = False


@dataclass(frozen=True, slots=True)
class AgentFinished:
    re: str
    kind: str
    result: SpawnResult | ReplyResult
    project: str | None
    qid: str | None
    sid: str | None
    now: float
    wall: float


@dataclass(frozen=True, slots=True)
class LinkReady:
    dev: str
    now: float


@dataclass(frozen=True, slots=True)
class LinkDown:
    pass


@dataclass(frozen=True, slots=True)
class Reconcile:
    """A pong reported the device's snapshot revs (05 §6.3 rule 6)."""

    device_revs: Any
    now: float
