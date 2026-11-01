"""Invariant checks for flow vectors (docs/05-architecture.md §6.3, §6.4).

check_flow() folds the steps of one flow into a frozen State and reports:
  - every line decodes to its declared outcome and travels in a direction its schema allows;
  - each delivered host-initiated message gets exactly one device reply (a detach,
    device_reboot or daemon_restart excuses messages still unanswered at that point);
  - replies only answer lines the replier has received;
  - host credit: unanswered host-initiated bytes <= host_inflight_max, reclaimed after
    credit_reclaim_ms; device in-flight caps for outbox / agent / queue+inbox requests;
  - computed expectations: host.credit_used, device.outbox_inflight, device.agent_inflight;
  - the host-side file.* invariants of file_flows.py (docs/11-m3-design.md §5.4);
  - the device-side rec.* invariants of rec_flows.py (docs/13 §12.2).
"""

from __future__ import annotations

import base64
from collections.abc import Callable, Mapping
from dataclasses import dataclass, replace
from functools import partial, reduce
from typing import Any

from file_flows import check_file_flow
from rec_flows import check_rec_flow
from schemas import Contract
from wire import Decoded, canonical_line, decode_line

DIRS = {"host": "h2d", "device": "d2h"}
CAPS = {
    "outbox": "device_outbox_inflight_max",
    "agent": "device_agent_inflight_max",
    "misc": "device_queue_inbox_inflight_max",
}


@dataclass(frozen=True)
class Sent:
    id: str
    size: int
    t: int


@dataclass(frozen=True)
class Request:
    id: str
    kind: str
    t: int


@dataclass(frozen=True)
class State:
    now: int = 0
    credit: tuple[Sent, ...] = ()
    replies_to_host: tuple[tuple[str, int], ...] = ()
    awaiting: tuple[str, ...] = ()
    answered: tuple[str, ...] = ()
    to_host: tuple[tuple[str, int], ...] = ()
    inflight: tuple[Request, ...] = ()
    errors: tuple[str, ...] = ()


def _fail(state: State, message: str) -> State:
    return replace(state, errors=state.errors + (message,))


def credit_used(contract: Contract, state: State, t: int) -> int:
    reclaim = contract.limits["credit_reclaim_ms"]
    answered = {re for re, delivered in state.replies_to_host if delivered <= t}
    return sum(s.size for s in state.credit if t < s.t + reclaim and s.id not in answered)


def active(contract: Contract, state: State, kind: str, t: int) -> tuple[Request, ...]:
    timeout = contract.limits["agent_timeout_ms"]
    return tuple(r for r in state.inflight if r.kind == kind and (kind != "agent" or t < r.t + timeout))


def _request_kind(t: str) -> str:
    if t == "outbox.item":
        return "outbox"
    return "agent" if t.startswith("agent.") else "misc"


def _unanswered(state: State) -> tuple[str, ...]:
    return tuple(i for i in state.awaiting if i not in state.answered)


def _host_initiated(contract: Contract, state: State, step: Mapping, raw: bytes, decoded: Decoded) -> State:
    mid, t = decoded.reply_id, step["t"]
    used = credit_used(contract, state, t) + len(raw)
    if used > contract.limits["host_inflight_max"]:
        state = _fail(state, f"credit window exceeded by {mid}: {used} > {contract.limits['host_inflight_max']} bytes")
    state = replace(state, credit=state.credit + (Sent(mid, len(raw), t),))
    if step.get("lost"):
        return state
    if decoded.outcome == "ok" and decoded.msg["t"] == "hello":
        state = replace(state, inflight=())  # device re-handshakes: in-flight items go back to pending
    return replace(state, awaiting=state.awaiting + (mid,))


def _host_reply(state: State, step: Mapping, msg: Mapping) -> State:
    re, t = msg["re"], step["t"]
    if not any(i == re and when <= t for i, when in state.to_host):
        state = _fail(state, f"host replies to {re}, which the host has not received by t={t}")
    if step.get("lost"):
        return state
    return replace(state, inflight=tuple(r for r in state.inflight if r.id != re))


def _device_reply(state: State, step: Mapping, msg: Mapping) -> State:
    re = msg["re"]
    if re not in state.awaiting:
        state = _fail(state, f"device replies to {re}, which is not a delivered host message")
    elif re in state.answered:
        state = _fail(state, f"second device reply to {re}")
    state = replace(state, answered=state.answered + (re,))
    if step.get("lost"):
        return state
    return replace(state, replies_to_host=state.replies_to_host + ((re, step.get("deliver_t", step["t"])),))


def _device_request(contract: Contract, state: State, step: Mapping, msg: Mapping) -> State:
    mid, t, kind = msg["id"], step["t"], _request_kind(msg["t"])
    live = active(contract, state, kind, t)
    if not any(r.id == mid for r in live):
        cap = contract.limits[CAPS[kind]]
        if len(live) + 1 > cap:
            state = _fail(state, f"{mid} exceeds the device {kind} in-flight cap of {cap}")
        state = replace(state, inflight=state.inflight + (Request(mid, kind, t),))
    if step.get("lost"):
        return state
    return replace(state, to_host=state.to_host + ((mid, step.get("deliver_t", t)),))


def _route(contract: Contract, state: State, step: Mapping, raw: bytes, decoded: Decoded) -> State:
    host = step["from"] == "host"
    if decoded.reply_id is not None:
        return (
            _host_initiated(contract, state, step, raw, decoded)
            if host
            else _device_request(contract, state, step, decoded.msg)
        )
    if decoded.outcome == "ok":
        return _host_reply(state, step, decoded.msg) if host else _device_reply(state, step, decoded.msg)
    return state


def _line(contract: Contract, state: State, label: str, step: Mapping) -> State:
    raw = canonical_line(step["msg"]) if "msg" in step else base64.b64decode(step["line_b64"])
    decoded = decode_line(contract, raw)
    want = step.get("outcome", "ok")
    if decoded.outcome != want:
        state = _fail(state, f"{label}: decodes as {decoded.outcome} {list(decoded.fields)}, step says {want}")
    if "deliver_t" in step and step["from"] != "device":
        state = _fail(state, f"{label}: deliver_t is only for device lines (TX parking)")
    if decoded.outcome == "ok":
        dirs = contract.schemas[decoded.msg["t"]].get("x-dir", ())
        if DIRS[step["from"]] not in dirs:
            state = _fail(state, f"{label}: {decoded.msg['t']} may not travel in direction {DIRS[step['from']]}")
    return _route(contract, state, step, raw, decoded)


def _event(state: State, name: str) -> State:
    if name == "user_spawn":
        return state
    host_side = dict(credit=(), replies_to_host=()) if name in ("detach", "daemon_restart") else {}
    device_side = dict(inflight=()) if name in ("detach", "device_reboot") else {}
    return replace(state, awaiting=(), answered=(), to_host=(), **host_side, **device_side)


COMPUTED: Mapping[str, Callable[[Contract, State, int], int]] = {
    "host.credit_used": credit_used,
    "device.outbox_inflight": lambda c, s, t: len(active(c, s, "outbox", t)),
    "device.agent_inflight": lambda c, s, t: len(active(c, s, "agent", t)),
}


def _expect(contract: Contract, state: State, label: str, step: Mapping) -> State:
    computed = (
        (key, want, COMPUTED[key](contract, state, step["t"]))
        for key, want in step["expect"].items()
        if key in COMPUTED
    )
    wrong = tuple(f"{label}: expected {key}={want}, computed {got}" for key, want, got in computed if got != want)
    return replace(state, errors=state.errors + wrong)


def _apply(contract: Contract, state: State, indexed: tuple[int, Mapping[str, Any]]) -> State:
    index, step = indexed
    label = f"step {index} (t={step['t']})"
    if step["t"] < state.now:
        state = _fail(state, f"{label}: time goes backwards ({step['t']} < {state.now})")
    state = replace(state, now=max(state.now, step["t"]))
    if "from" in step:
        return _line(contract, state, label, step)
    if "event" in step:
        return _event(state, step["event"])
    return _expect(contract, state, label, step)


def check_flow(contract: Contract, flow: Mapping[str, Any]) -> tuple[str, ...]:
    final = reduce(partial(_apply, contract), enumerate(flow["steps"]), State())
    missing = tuple(f"host message {i} got no device reply" for i in _unanswered(final))
    return final.errors + missing + check_file_flow(contract, flow) + check_rec_flow(contract, flow)
