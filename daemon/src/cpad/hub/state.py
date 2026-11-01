"""HubState: the daemon's single, immutable state value (05 §7.3). Only reduce() produces new ones."""

from __future__ import annotations

from dataclasses import dataclass, field

from cpad.inbox.need import Dismissed, Tracked
from cpad.projects.registry import Project
from cpad.protocol.messages import Caps, Revs
from cpad.queue.fold import QueueState


@dataclass(frozen=True, slots=True)
class AgentInfo:
    present: bool = False
    version: str = ""
    errored: bool = False
    reply: bool = False

    @property
    def status(self) -> str:
        return "error" if self.errored else "ok" if self.present else "missing"

    @property
    def caps(self) -> Caps:
        return Caps(spawn=self.present, reply=self.present and self.reply)


@dataclass(frozen=True, slots=True)
class Published:
    """Revs last assigned per snapshot kind and the content keys they were assigned for."""

    revs: Revs = Revs()
    keys: tuple = (None, None, None)


@dataclass(frozen=True, slots=True)
class HubState:
    projects: tuple[Project, ...] = ()
    tracked: tuple[Tracked, ...] = ()
    dismissed: Dismissed = field(default_factory=frozenset)
    queue: QueueState = QueueState()
    published: Published = Published()
    agent: AgentInfo = AgentInfo()
    agent_busy: str | None = None
    spawn_seq: int = 0
    dev: str | None = None
    idle_s: float = 60.0
