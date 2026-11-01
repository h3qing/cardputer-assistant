"""reduce(state, input) -> (state, effects): the daemon's only state transition (05 §7.3).

Effect order is the contract the shell keeps: Append (fsync) always precedes the Send that
acknowledges it. Queued intents (spawn / feedback) are recorded and never executed; execution
happens only for online agent.* requests that pass every check.
"""

from __future__ import annotations

from dataclasses import replace
from functools import singledispatch
from typing import Any

from cpad.agents.base import ReplyRequest, ReplyResult, SpawnRequest
from cpad.agents.claude_code.argv import check_text, session_name
from cpad.events import records
from cpad.hub import effects as fx
from cpad.hub import inputs
from cpad.hub.snapshots import KINDS, behind, publish
from cpad.hub.state import AgentInfo, HubState
from cpad.inbox.need import dismiss, prune, track
from cpad.notes.render import KNOWN_DESTS
from cpad.projects.registry import find as find_project
from cpad.protocol import messages as m
from cpad.protocol.codec import Decoded
from cpad.protocol.validate import matches
from cpad.queue.fold import find as find_item
from cpad.queue.fold import fold_queue

Result = tuple[HubState, tuple[Any, ...]]
MONOID = r"^[0-9A-HJKMNP-TV-Z]{7}-[0-9A-HJKMNP-TV-Z]{5}-[0-9A-HJKMNP-TV-Z]{4}$"


def reduce(state: HubState, event: Any) -> Result:
    return _reduce(event, state)


def _published(state: HubState, now: float, everything: bool = False, also: tuple[str, ...] = ()) -> Result:
    published, snaps, changed = publish(state, now)
    kinds = KINDS if everything else tuple(k for k in KINDS if k in changed or k in also)
    return replace(state, published=published), tuple(fx.Push(snaps.get(k)) for k in kinds)


@singledispatch
def _reduce(event: Any, state: HubState) -> Result:
    raise TypeError(f"unknown hub input {type(event).__name__}")


@_reduce.register
def _(event: inputs.DeviceRequest, state: HubState) -> Result:
    return _request(event.msg, state, event)


@_reduce.register
def _(event: inputs.Appended, state: HubState) -> Result:
    return _published(replace(state, queue=fold_queue(event.records, state.queue)), event.now)


@_reduce.register
def _(event: inputs.Polled, state: HubState) -> Result:
    tracked = track(state.tracked, event.sessions, event.now)
    return _published(replace(state, tracked=tracked, dismissed=prune(state.dismissed, tracked)), event.now)


@_reduce.register
def _(event: inputs.ProjectsLoaded, state: HubState) -> Result:
    return _published(replace(state, projects=event.projects), event.now)


@_reduce.register
def _(event: inputs.AgentProbed, state: HubState) -> Result:
    return replace(state, agent=AgentInfo(event.present, event.version, event.errored, event.reply)), ()


@_reduce.register
def _(event: inputs.LinkReady, state: HubState) -> Result:
    return _published(replace(state, dev=event.dev), event.now, everything=True)


@_reduce.register
def _(event: inputs.Reconcile, state: HubState) -> Result:
    return _published(state, event.now, also=behind(state.published.revs, event.device_revs))


@_reduce.register
def _(event: inputs.LinkDown, state: HubState) -> Result:
    return state, ()


@_reduce.register
def _(event: inputs.AgentFinished, state: HubState) -> Result:
    state = replace(state, agent_busy=None)
    at = records.iso(event.wall)
    if event.kind == "spawn":
        result = event.result
        if not result.ok:
            code = result.code or "E_SPAWN"
            rec = records.agent_spawn_failed(event.project or "", result.name, code, at, why=result.why)
            return state, (fx.Append((rec,)), fx.Send(m.AgentResult(event.re, False, code, why=result.why)))
        spawned = (records.queue_spawned(event.qid, at, result.name, result.short),) if event.qid else ()
        recs = (records.agent_spawned(event.project or "", result.name, result.short, at), *spawned)
        return state, (fx.Append(recs), fx.Send(m.AgentResult(event.re, True, name=result.name, short=result.short)))
    return _replied(state, event, at)


def _replied(state: HubState, event: inputs.AgentFinished, at: str) -> Result:
    result: ReplyResult = event.result
    if result.forked:
        state = replace(state, agent=replace(state.agent, reply=False))
    sid = event.sid or ""
    rec = records.agent_replied(sid, at) if result.ok else records.agent_reply_failed(sid, result.code or "", at)
    reply = m.AgentResult(event.re, True) if result.ok else m.AgentResult(event.re, False, result.code)
    return state, (fx.Append((rec,)), fx.Send(reply))


# --- device requests -------------------------------------------------------------------------


@singledispatch
def _request(msg: Any, state: HubState, event: inputs.DeviceRequest) -> Result:
    return state, (fx.Send(m.ErrorMsg(msg.id, "E_UNKNOWN_TYPE")),)


@_request.register
def _(msg: m.OutboxItem, state: HubState, event: inputs.DeviceRequest) -> Result:
    if event.duplicate:
        return state, (fx.Send(m.OutboxAck(msg.id, "duplicate")),)
    at = records.iso(event.wall)
    project = msg.body.project
    orphan = project is not None and find_project(state.projects, project) is None
    received = records.item_received(state.dev or "", msg, received_at=at, orphan=orphan)
    if msg.kind == "queue":
        recs = (received, records.queue_added_from_item(msg, at))
        return state, (fx.Append(recs), fx.Send(m.OutboxAck(msg.id, "stored")))
    recs = (received, *_downgraded(msg, at))
    return state, (fx.Append(recs), fx.Send(m.OutboxAck(msg.id, "stored")), fx.WriteNote(received))


def _downgraded(msg: m.OutboxItem, at: str) -> tuple[dict[str, Any], ...]:
    """A dest this cpad does not know goes to the daily note, never anywhere else (docs/13 §9.2)."""
    dest = msg.body.dest
    return () if dest is None or dest in KNOWN_DESTS else (records.dest_downgraded(msg.id, dest, at),)


def _refuse(re: str, code: str) -> tuple[Any, ...]:
    return (fx.Send(m.AgentResult(re, False, code)),)


def _spawn_text(msg: m.AgentSpawn, state: HubState) -> tuple[str | None, str | None]:
    """(text, error code): with a qid the saved full text is the prompt (05 §7.7)."""
    if msg.qid is None:
        return msg.text, check_text(msg.text)
    item = find_item(state.queue, msg.qid)
    if item is None or item.project != msg.project:
        return None, "E_UNKNOWN_ITEM"
    return item.text, check_text(item.text)


def _spawn_mode(msg: m.AgentSpawn, state: HubState, mode: str) -> str:
    """A spoken item (one handed from a recording) always starts in plan mode, whatever the project's mode."""
    item = find_item(state.queue, msg.qid) if msg.qid is not None else None
    return "plan" if item is not None and item.voice else mode


@_request.register
def _(msg: m.AgentSpawn, state: HubState, event: inputs.DeviceRequest) -> Result:
    project = find_project(state.projects, msg.project)
    text, code = _spawn_text(msg, state)
    first_failure = (
        (state.agent_busy is not None and "E_BUSY")
        or ((project is None or not project.ok or not project.linked) and "E_UNKNOWN_PROJECT")
        or (not state.agent.present and "E_NO_AGENT")
        or code
    )
    if first_failure:
        return state, _refuse(msg.id, first_failure)
    n, mode = state.spawn_seq + 1, _spawn_mode(msg, state, project.mode)
    request = SpawnRequest(project.root, session_name(project.key, text, n), text, mode)
    state = replace(state, agent_busy=msg.id, spawn_seq=n)
    return state, (fx.RunSpawn(msg.id, request, project.key, msg.qid),)


@_request.register
def _(msg: m.AgentReply, state: HubState, event: inputs.DeviceRequest) -> Result:
    if state.agent_busy is not None:
        return state, _refuse(msg.id, "E_BUSY")
    if not state.agent.caps.reply:
        return state, _refuse(msg.id, "E_NOT_REPLYABLE")
    hits = [t for t in state.tracked if t.info.sid == msg.sid]
    if len(hits) != 1:
        return state, _refuse(msg.id, "E_UNKNOWN_SESSION")
    if hits[0].info.kind != "bg" or hits[0].info.status != "idle":
        return state, _refuse(msg.id, "E_NOT_REPLYABLE")
    return replace(state, agent_busy=msg.id), (fx.RunReply(msg.id, ReplyRequest(msg.sid, msg.text)),)


@_request.register
def _(msg: m.QueueDone, state: HubState, event: inputs.DeviceRequest) -> Result:
    if find_item(state.queue, msg.qid) is None:
        return state, (fx.Send(m.ErrorMsg(msg.id, "E_UNKNOWN_ITEM")),)
    return state, (fx.Append((records.queue_done(msg.qid, records.iso(event.wall)),)), fx.Send(m.Ack(msg.id)))


@_request.register
def _(msg: m.InboxDismiss, state: HubState, event: inputs.DeviceRequest) -> Result:
    hits = [t for t in state.tracked if t.info.sid == msg.sid]
    if len(hits) != 1:
        return state, (fx.Send(m.ErrorMsg(msg.id, "E_UNKNOWN_SESSION")),)
    state, pushes = _published(replace(state, dismissed=dismiss(state.dismissed, hits[0])), event.now)
    return state, (fx.Send(m.Ack(msg.id)), *pushes)


# --- replies to invalid or unsupported device requests ----------------------------------------


def _field(decoded: Decoded) -> str | None:
    field = decoded.fields[0] if decoded.fields else None
    return field if matches(r"^[!-~]{1,48}$", field) else None


def failure_reply(decoded: Decoded) -> Any:
    """The one reply owed to a device request that cannot be served (protocol/README reply table)."""
    msg, re = decoded.msg or {}, decoded.reply_id or ""
    if decoded.outcome in ("E_VERSION", "E_UNKNOWN_TYPE"):
        return m.ErrorMsg(re, decoded.outcome)
    if decoded.ok:
        return m.ErrorMsg(re, "E_UNKNOWN_TYPE")
    if msg.get("t") == "outbox.item" and matches(MONOID, re):
        return m.OutboxAck(re, "rejected", "E_BAD_FIELD")
    if msg.get("t") in ("agent.spawn", "agent.reply"):
        return m.AgentResult(re, False, "E_BAD_FIELD")
    return m.ErrorMsg(re, "E_BAD_FIELD", _field(decoded))
