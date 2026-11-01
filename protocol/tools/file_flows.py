"""Host-side file.* invariants for flow vectors (docs/11-m3-design.md §5.4, §5.7, §6.2).

check_file_flow() folds the steps of one flow into a frozen FileState and reports:
  - gate: the host sends file.list / file.get / file.del only while the latest hello or pong it has
    received carries files with live:false and rev > 0 (a device without files is never asked), with
    at most file_inflight_max of them unanswered;
  - E_BUSY: once an E_BUSY reply to any file.* has reached the host, it sends no file.* until a later
    hello or pong shows files.live:false;
  - reply kind: file.list -> file.items | error; file.get -> file.data | error, with the same fid and off
    and n <= len; file.del, debug.rec, debug.mic and debug.codec -> ack | error;
  - file.del is sent only after file.data replies delivered to the host cover every byte [0, size) of
    that file (received bytes survive detach / restart events: the daemon keeps them in its .part file).
That every host message gets exactly one reply is checked for all types by flows.check_flow.
A device line counts for the host at its delivery time (deliver_t, TX parking); lost lines never do.
"""

from __future__ import annotations

import base64
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, replace
from functools import partial, reduce
from typing import Any

from schemas import Contract
from wire import canonical_line, decode_line

FILE_REQUESTS = frozenset({"file.list", "file.get", "file.del"})
REPLY_KINDS: Mapping[str, frozenset[str]] = {
    "file.list": frozenset({"file.items", "error"}),
    "file.get": frozenset({"file.data", "error"}),
    "file.del": frozenset({"ack", "error"}),
    "debug.rec": frozenset({"ack", "error"}),
    "debug.mic": frozenset({"ack", "error"}),
    "debug.codec": frozenset({"ack", "error"}),
}
RESETS = frozenset({"detach", "daemon_restart", "device_reboot"})


@dataclass(frozen=True)
class Heard:
    """A device line as the host received it: delivery time, step index, message."""

    when: int
    seq: int
    msg: Mapping[str, Any]


@dataclass(frozen=True)
class Sent:
    t: int
    msg: Mapping[str, Any]


@dataclass(frozen=True)
class FileState:
    status: tuple[Heard, ...] = ()
    busy: tuple[Heard, ...] = ()
    chunks: tuple[Heard, ...] = ()
    requests: tuple[Sent, ...] = ()
    replies: tuple[Heard, ...] = ()
    errors: tuple[str, ...] = ()


def _known(heard: Iterable[Heard], t: int) -> tuple[Heard, ...]:
    return tuple(h for h in heard if h.when <= t)


def _latest(heard: Iterable[Heard]) -> Heard | None:
    return max(heard, key=lambda h: (h.when, h.seq), default=None)


def _idle(heard: Heard) -> bool:
    files = heard.msg.get("files")
    return isinstance(files, Mapping) and files.get("live") is False


def _status_problem(state: FileState, t: int) -> str:
    latest = _latest(_known(state.status, t))
    files = latest.msg.get("files") if latest else None
    if not isinstance(files, Mapping):
        return "the device has not advertised files (v0.2 device or no hello/pong yet)"
    if files.get("live"):
        return "files.live is true"
    return "files.rev is 0 (index not ready)" if files.get("rev") == 0 else ""


def _busy_problem(state: FileState, t: int) -> str:
    status = _known(state.status, t)
    for busy in _known(state.busy, t):
        cleared = any(_idle(s) and (s.when, s.seq) > (busy.when, busy.seq) for s in status)
        if not cleared:
            return f"after E_BUSY {busy.msg['id']} and before a hello/pong with files.live:false"
    return ""


def _inflight_problem(contract: Contract, state: FileState, t: int) -> str:
    reclaim, cap = contract.limits["credit_reclaim_ms"], contract.limits["file_inflight_max"]
    answered = {r.msg["re"] for r in _known(state.replies, t)}
    open_requests = tuple(
        s for s in state.requests if s.msg["t"] in FILE_REQUESTS and s.msg["id"] not in answered and t < s.t + reclaim
    )
    return f"more than {cap} file.* requests in flight" if len(open_requests) >= cap else ""


def _covered(chunks: Iterable[Heard]) -> int:
    """Length of the contiguous prefix [0, end) covered by the chunks."""
    end = 0
    for start, stop in sorted((c.msg["off"], c.msg["off"] + c.msg["n"]) for c in chunks):
        if start > end:
            break
        end = max(end, stop)
    return end


def _delete_problem(state: FileState, t: int, msg: Mapping[str, Any]) -> str:
    chunks = tuple(c for c in _known(state.chunks, t) if c.msg["fid"] == msg["fid"])
    sizes = {c.msg["size"] for c in chunks}
    if sizes and sizes != {msg["size"]}:
        return f"size {msg['size']} differs from the pulled size {sorted(sizes)}"
    got = min(_covered(chunks), msg["size"])
    return "" if got == msg["size"] else f"before the host received every byte ({got}/{msg['size']} B)"


def _request_problem(contract: Contract, state: FileState, t: int, msg: Mapping[str, Any]) -> str:
    if msg["t"] not in FILE_REQUESTS:
        return ""
    checks = (
        lambda: _busy_problem(state, t),
        lambda: _status_problem(state, t),
        lambda: _inflight_problem(contract, state, t),
        lambda: _delete_problem(state, t, msg) if msg["t"] == "file.del" else "",
    )
    problem = next((p for p in (check() for check in checks) if p), "")
    return f"{msg['id']}: {msg['t']} sent {problem}" if problem else ""


def _reply_problem(request: Mapping[str, Any], reply: Mapping[str, Any]) -> str:
    rid, kind = request["id"], request["t"]
    if reply["t"] not in REPLY_KINDS[kind]:
        return f"{rid}: {kind} answered by {reply['t']}"
    if reply["t"] != "file.data":
        return ""
    if (reply["fid"], reply["off"]) != (request["fid"], request["off"]) or reply["n"] > request["len"]:
        return f"{rid}: file.data does not answer the file.get (fid, off or n > len)"
    return ""


def _fail(state: FileState, problem: str) -> FileState:
    return replace(state, errors=state.errors + (problem,)) if problem else state


def _host_line(contract: Contract, state: FileState, step: Mapping, msg: Mapping[str, Any]) -> FileState:
    if msg["t"] not in REPLY_KINDS or "re" in msg:
        return state
    state = _fail(state, _request_problem(contract, state, step["t"], msg))
    return replace(state, requests=state.requests + (Sent(step["t"], msg),))


def _heard(state: FileState, heard: Heard, request: Mapping[str, Any] | None) -> FileState:
    msg = heard.msg
    if msg["t"] in ("hello", "pong"):
        return replace(state, status=state.status + (heard,))
    if request is None:
        return state
    state = replace(state, replies=state.replies + (heard,))
    if msg["t"] == "file.data":
        return replace(state, chunks=state.chunks + (heard,))
    if msg["t"] == "error" and msg.get("code") == "E_BUSY" and request["t"] in FILE_REQUESTS:
        return replace(state, busy=state.busy + (heard,))
    return state


def _device_line(state: FileState, seq: int, step: Mapping, msg: Mapping[str, Any]) -> FileState:
    request = next((s.msg for s in reversed(state.requests) if s.msg["id"] == msg.get("re")), None)
    if request is not None:
        state = _fail(state, _reply_problem(request, msg))
    if step.get("lost"):
        return state
    return _heard(state, Heard(step.get("deliver_t", step["t"]), seq, msg), request)


def _apply(contract: Contract, state: FileState, indexed: tuple[int, Mapping[str, Any]]) -> FileState:
    seq, step = indexed
    if step.get("event") in RESETS:
        return replace(state, status=(), requests=(), replies=())
    if "from" not in step:
        return state
    raw = canonical_line(step["msg"]) if "msg" in step else base64.b64decode(step["line_b64"])
    decoded = decode_line(contract, raw)
    if decoded.outcome != "ok":
        return state
    if step["from"] == "host":
        return _host_line(contract, state, step, decoded.msg)
    return _device_line(state, seq, step, decoded.msg)


def check_file_flow(contract: Contract, flow: Mapping[str, Any]) -> tuple[str, ...]:
    return reduce(partial(_apply, contract), enumerate(flow["steps"]), FileState()).errors
