"""Device-side rec.* invariants for flow vectors (docs/13 §8.1, §8.2, §12.2).

check_rec_flow() folds the steps of one flow into a frozen RecState and reports:
  - gate: the device sends rec.list / rec.act only while the latest host hello it has received carries
    caps.recs:true (a detach or device_reboot forgets it; a daemon_restart does not, the device cannot see one);
  - reply kind: rec.list -> rec.items | error, rec.act -> ack | error; a rec.items has at most the max its
    rec.list asked for;
  - cursor: a rec.list cursor is the next of a rec.items the device has received (it never makes one up).
That every device request gets at most one reply, and the shared in-flight slot, are checked by flows.check_flow.
"""

from __future__ import annotations

import base64
from collections.abc import Mapping
from dataclasses import dataclass, replace
from functools import partial, reduce
from typing import Any

from schemas import Contract
from wire import canonical_line, decode_line

REPLY_KINDS: Mapping[str, frozenset[str]] = {
    "rec.list": frozenset({"rec.items", "error"}),
    "rec.act": frozenset({"ack", "error"}),
}
FORGET = frozenset({"detach", "device_reboot"})


@dataclass(frozen=True)
class RecState:
    recs: bool = False
    requests: tuple[Mapping[str, Any], ...] = ()
    cursors: tuple[str, ...] = ()
    errors: tuple[str, ...] = ()


def _fail(state: RecState, problem: str) -> RecState:
    return replace(state, errors=state.errors + (problem,)) if problem else state


def _request_problem(state: RecState, msg: Mapping[str, Any]) -> str:
    if not state.recs:
        return f"{msg['id']}: {msg['t']} sent although the latest host hello did not carry caps.recs:true"
    cursor = msg.get("cursor")
    if msg["t"] == "rec.list" and cursor is not None and cursor not in state.cursors:
        return f"{msg['id']}: rec.list cursor {cursor!r} is not the next of a page the device received"
    return ""


def _reply_problem(request: Mapping[str, Any], reply: Mapping[str, Any]) -> str:
    rid, kind = request["id"], request["t"]
    if reply["t"] not in REPLY_KINDS[kind]:
        return f"{rid}: {kind} answered by {reply['t']}"
    rows = len(reply.get("items") or ()) if reply["t"] == "rec.items" else 0
    return f"{rid}: rec.items has {rows} rows, max {request['max']}" if rows > request.get("max", rows) else ""


def _host_line(state: RecState, step: Mapping, msg: Mapping[str, Any]) -> RecState:
    if msg["t"] == "hello" and msg.get("role") == "host" and not step.get("lost"):
        return replace(state, recs=(msg.get("caps") or {}).get("recs") is True)
    request = next((r for r in reversed(state.requests) if r["id"] == msg.get("re")), None)
    if request is None:
        return state
    state = _fail(state, _reply_problem(request, msg))
    cursor = msg.get("next") if msg["t"] == "rec.items" and not step.get("lost") else None
    return replace(state, cursors=state.cursors + (cursor,)) if cursor else state


def _device_line(state: RecState, msg: Mapping[str, Any]) -> RecState:
    if msg["t"] not in REPLY_KINDS or "re" in msg:
        return state
    return replace(_fail(state, _request_problem(state, msg)), requests=state.requests + (msg,))


def _apply(contract: Contract, state: RecState, step: Mapping[str, Any]) -> RecState:
    if step.get("event") in FORGET:
        return replace(state, recs=False, cursors=() if step["event"] == "device_reboot" else state.cursors)
    if "from" not in step:
        return state
    raw = canonical_line(step["msg"]) if "msg" in step else base64.b64decode(step["line_b64"])
    decoded = decode_line(contract, raw)
    if decoded.outcome != "ok":
        return state
    if step["from"] == "host":
        return _host_line(state, step, decoded.msg)
    return _device_line(state, decoded.msg)


def check_rec_flow(contract: Contract, flow: Mapping[str, Any]) -> tuple[str, ...]:
    return reduce(partial(_apply, contract), flow["steps"], RecState()).errors
