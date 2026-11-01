"""EffectRunner: executes hub effects in order (05 §7.1). The only place hub decisions touch I/O.

Append writes and fsyncs events.jsonl before any later effect runs, so an ack is never sent for an
item that is not on disk; if the append fails, the rest of the batch (the ack) is dropped and the
device simply retries. Agent runs are background tasks whose results come back as hub inputs.
Lines another process appended first (`cpa rec unhide`) come back from the append as `others`: they go to the
hub with ours and, with ours, to `absorb` (Recordings: its filing fold needs queue.spawned / queue.done, docs/23
§2.2), since the shared offset has moved past them and no poll() sees them.
"""

from __future__ import annotations

import asyncio
import logging
import time
from collections.abc import Callable
from typing import Any

from cpad.agents.base import AgentAdapter, ReplyResult, SpawnResult
from cpad.events.log import EventLog
from cpad.hub import effects as fx
from cpad.hub import inputs
from cpad.hub.state import HubState
from cpad.notes.writer import NotesWriter
from cpad.queue.fold import assign_qids, fold_queue

log = logging.getLogger("cpad.hub")


class EffectRunner:
    def __init__(self, *, events: EventLog, notes: NotesWriter, adapter: AgentAdapter, get_link: Callable[[], Any],
                 get_state: Callable[[], HubState], dispatch: Callable[[Any], None],
                 absorb: Callable[[tuple[Any, ...]], None] = lambda _others: None,
                 clock: Callable[[], float] = time.monotonic, wall: Callable[[], float] = time.time):  # fmt: skip
        self._events, self._notes, self._adapter = events, notes, adapter
        self._link, self._state, self._dispatch, self._absorb = get_link, get_state, dispatch, absorb
        self._clock, self._wall = clock, wall
        self._tasks: set[asyncio.Task] = set()

    def run(self, effects: tuple[Any, ...]) -> None:
        for effect in effects:
            if not self._one(effect):
                return

    def _one(self, effect: Any) -> bool:
        link = self._link()
        if isinstance(effect, fx.Append):
            return self._append(effect)
        if isinstance(effect, fx.Send | fx.Push):
            if link is not None and link.ready:
                (link.reply if isinstance(effect, fx.Send) else link.request)(effect.msg)
        elif isinstance(effect, fx.WriteNote):
            self._notes.write(effect.record)
        elif isinstance(effect, fx.RunSpawn | fx.RunReply):
            task = asyncio.get_running_loop().create_task(self._agent(effect))
            self._tasks = self._tasks | {task}
            task.add_done_callback(lambda t: setattr(self, "_tasks", self._tasks - {t}))
        return True

    def _append(self, effect: fx.Append) -> bool:
        queue = self._state().queue
        try:
            others, written = self._events.append(
                effect.records, prepare=lambda new: assign_qids(effect.records, fold_queue(new, queue))
            )
        except OSError as exc:
            log.error("events.jsonl append failed; not acknowledging", extra={"fields": {"err": exc.errno}})
            return False
        self._absorb((*others, *written))
        self._dispatch(inputs.Appended((*others, *written), self._clock()))
        return True

    async def _agent(self, effect: fx.RunSpawn | fx.RunReply) -> None:
        spawn = isinstance(effect, fx.RunSpawn)
        try:
            result: SpawnResult | ReplyResult = await (
                self._adapter.spawn(effect.request) if spawn else self._adapter.reply(effect.request)
            )
        except Exception:
            log.exception("agent request crashed")
            result = SpawnResult(False, code="E_INTERNAL") if spawn else ReplyResult(False, "E_INTERNAL")
        project, qid = (effect.project, effect.qid) if spawn else (None, None)
        sid = None if spawn else effect.request.sid
        kind = "spawn" if spawn else "reply"
        self._dispatch(inputs.AgentFinished(effect.re, kind, result, project, qid, sid, self._clock(), self._wall()))

    async def drain(self) -> None:
        if self._tasks:
            await asyncio.gather(*self._tasks, return_exceptions=True)
