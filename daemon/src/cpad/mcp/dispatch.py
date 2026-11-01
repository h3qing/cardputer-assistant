"""One request in, one reply and one content-free audit Call out (docs/15 §3.3, §3.6, §3.8, §3.9). Pure.

The state is the legacy session (if any), the two rate buckets and a call counter. One result shape
serves both eras: resultType and _meta.serverInfo always, ttlMs and cacheScope on list and read
results (handshake-era results allow extra fields). The server never sends requests of its own.
A write tool runs only after Env.intent has recorded its audit line; if that fails, it does not run.
"""

from __future__ import annotations

from collections.abc import Callable, Mapping
from dataclasses import dataclass, field, replace
from typing import Any

from cpad import __version__
from cpad.mcp import catalog, era, resources, tools
from cpad.mcp.audit import AuditError, Call
from cpad.mcp.catalog import ToolSpec
from cpad.mcp.jsonrpc import INTERNAL_ERROR, INVALID_PARAMS, METHOD_NOT_FOUND, Request, error, result
from cpad.mcp.limits import READ, WRITE, Bucket, dumps, full, retry_after, take
from cpad.mcp.tools import Ctx, Outcome, ToolError

META_SERVER = "io.modelcontextprotocol/serverInfo"
SERVER_INFO = {"name": "cpa", "title": "Cardputer Assistant archive", "version": __version__}
CAPABILITIES = {"tools": {"listChanged": False}, "resources": {"listChanged": False, "subscribe": False}}
STATIC_TTL_MS = 3_600_000  # the catalogue never changes while the process runs
DATA_TTL_MS = 0
UNAUDITED = "Done, but the audit log could not record the outcome (its intent line was written)"

__all__ = ["Bucket", "Env", "State", "audit_failed", "handle"]


@dataclass(frozen=True, slots=True)
class Env:
    ctx: Ctx
    tools: tuple[ToolSpec, ...]
    intent: Callable[[Call], None] | None = None  # writes a write tool's audit line before it runs


@dataclass(frozen=True, slots=True)
class State:
    session: era.Session = field(default_factory=era.Session)
    read: Bucket | None = None
    write: Bucket | None = None
    calls: int = 0


Reply = dict[str, Any]
Handled = tuple[State, Reply, Call | None]


def complete(body: Mapping[str, Any], ttl_ms: int | None = None) -> dict[str, Any]:
    out = {**body, "resultType": "complete", "_meta": {META_SERVER: SERVER_INFO}}
    return out if ttl_ms is None else {**out, "ttlMs": ttl_ms, "cacheScope": "private"}


def _take(state: State, now: float, write: bool) -> tuple[State, float | None]:
    rate = WRITE if write else READ
    bucket, ok = take((state.write if write else state.read) or full(rate, now), now, rate)
    state = replace(state, write=bucket) if write else replace(state, read=bucket)
    return state, None if ok else retry_after(bucket, rate)


def _limited(req: Request, wait: float) -> Reply:
    return error(req.id, INTERNAL_ERROR, "Rate limited", {"retry_after_s": wait})


def _initialize(state: State, req: Request) -> Handled:
    found = era.initialize(state.session, req.params)
    if isinstance(found, era.Fault):
        return state, error(req.id, found.code, found.message), Call(req.method, "legacy", error=found.code)
    session, version = found
    body = {"protocolVersion": version, "capabilities": CAPABILITIES, "serverInfo": SERVER_INFO,
            "instructions": catalog.INSTRUCTIONS}  # fmt: skip
    return replace(state, session=session), result(req.id, complete(body)), Call(req.method, "legacy", session.client)


def _ping(state: State, req: Request) -> Handled:
    if state.session.legacy is None and era.has_modern_version(req.params):
        return state, error(req.id, METHOD_NOT_FOUND, "Method not found"), None
    return state, result(req.id, {}), None


def _discover(state: State, req: Request, found: era.Era) -> Handled:
    call = Call(req.method, found.name, found.client)
    if found.name != "modern":
        return state, error(req.id, METHOD_NOT_FOUND, "Method not found"), replace(call, error=METHOD_NOT_FOUND)
    body = {"supportedVersions": list(era.MODERN), "capabilities": CAPABILITIES, "instructions": catalog.INSTRUCTIONS}
    return state, result(req.id, complete(body, STATIC_TTL_MS)), call


def _tools_list(state: State, req: Request, env: Env, call: Call) -> Handled:
    if req.params.get("cursor") is not None:
        return state, error(req.id, INVALID_PARAMS, "Invalid cursor"), replace(call, error=INVALID_PARAMS)
    body = {"tools": [t.listing() for t in env.tools]}
    return state, result(req.id, complete(body, STATIC_TTL_MS)), replace(call, n=len(env.tools))


def tool_result(outcome: Outcome) -> dict[str, Any]:
    if outcome.error is not None:
        return complete({"content": [{"type": "text", "text": dumps(outcome.error.body())}], "isError": True})
    body = dict(outcome.body or {})
    return complete({"content": [{"type": "text", "text": dumps(body)}], "structuredContent": body, "isError": False})


def _announced(env: Env, spec: ToolSpec, arguments: Any, call: Call) -> bool:
    """A write tool's intent line, written before it runs; False when it could not be written."""
    if not spec.write or env.intent is None:
        return True
    ids, sizes = tools.trace(spec.name, arguments)
    try:
        env.intent(replace(call, ids=ids, sizes=sizes, phase="intent"))
    except AuditError:
        return False
    return True


def _run_tool(env: Env, spec: ToolSpec, arguments: Any, call: Call, wait: float | None) -> Outcome:
    if wait is not None:
        return Outcome(error=ToolError("E_RATE_LIMITED", retry_after_s=wait))
    if not _announced(env, spec, arguments, call):
        return Outcome(error=ToolError("E_AUDIT"))
    return tools.call(env.ctx, spec.name, arguments)


def _tool_call(state: State, req: Request, env: Env, call: Call, now: float) -> Handled:
    spec = catalog.find(env.tools, req.params.get("name"))
    arguments = req.params.get("arguments")
    if spec is None or not isinstance(arguments, dict | None):
        reply = error(req.id, INVALID_PARAMS, "Unknown tool" if spec is None else "arguments must be an object")
        return state, reply, replace(call, error=INVALID_PARAMS)
    keys = tuple(k for k in arguments or {} if k in spec.input_schema["properties"])
    call = replace(call, tool=spec.name, args=keys, phase="outcome" if spec.write else None)
    state, wait = _take(state, now, spec.write)
    outcome = _run_tool(env, spec, arguments, call, wait)
    call = replace(call, ids=outcome.ids, n=outcome.n, sizes=outcome.sizes,
                   error=outcome.error.code if outcome.error else None)  # fmt: skip
    return state, result(req.id, tool_result(outcome)), call


def _resources_list(state: State, req: Request, env: Env, call: Call) -> Handled:
    body = resources.list_resources(env.ctx, req.params.get("cursor"))
    return state, result(req.id, complete(body, DATA_TTL_MS)), replace(call, n=len(body["resources"]))


def _templates(state: State, req: Request, env: Env, call: Call) -> Handled:
    return state, result(req.id, complete(resources.templates(), STATIC_TTL_MS)), replace(call, n=3)


def _resources_read(state: State, req: Request, env: Env, call: Call) -> Handled:
    uri = req.params.get("uri")
    ref = resources.parse_uri(uri)
    call = replace(call, ids=(ref.key,))
    try:
        body = resources.read(env.ctx, uri)
    except resources.ResourceError as exc:
        return state, error(req.id, exc.code, exc.message), replace(call, error=exc.code)
    return state, result(req.id, complete(body, DATA_TTL_MS)), replace(call, n=1)


Route = Callable[[State, Request, Env, Call], Handled]
ROUTES: Mapping[str, Route] = {
    "tools/list": _tools_list,
    "resources/list": _resources_list,
    "resources/templates/list": _templates,
    "resources/read": _resources_read,
}


def _route(state: State, req: Request, env: Env, now: float, found: era.Era) -> Handled:
    call = Call(req.method, found.name, found.client)
    if req.method == "server/discover":
        return _discover(state, req, found)
    if req.method not in ROUTES and req.method != "tools/call":
        return state, error(req.id, METHOD_NOT_FOUND, "Method not found"), replace(call, error=METHOD_NOT_FOUND)
    if req.method == "tools/call":
        return _tool_call(state, req, env, call, now)
    state, wait = _take(state, now, write=False)
    if wait is not None:
        return state, _limited(req, wait), replace(call, error="E_RATE_LIMITED")
    try:
        return ROUTES[req.method](state, req, env, call)
    except resources.ResourceError as exc:
        return state, error(req.id, exc.code, exc.message), replace(call, error=exc.code)


def _dispatch(state: State, req: Request, env: Env, now: float) -> Handled:
    if req.method == "initialize":
        return _initialize(state, req)
    if req.method == "ping":
        return _ping(state, req)
    found = era.era_of(state.session, req.params)
    if isinstance(found, era.Fault):
        return state, error(req.id, found.code, found.message, found.data), Call(req.method, error=found.code)
    return _route(state, req, env, now, found)


def handle(state: State, req: Request, env: Env, now: float) -> Handled:
    """Never raises: an unexpected failure becomes -32603 with no detail."""
    counted = replace(state, calls=state.calls + 1)
    try:
        return _dispatch(counted, req, env, now)
    except Exception:  # noqa: BLE001 - a bug must not kill the server or leak its message
        return counted, error(req.id, INTERNAL_ERROR, "Internal error"), Call(req.method, error="internal")


def audit_failed(req: Request, reply: Reply | None = None, call: Call | None = None) -> Reply:
    """The reply that replaces a result whose audit line could not be written. Data is withheld; but a write
    that already happened is reported as done (with a warning), so the client does not repeat it."""
    done = call is not None and call.phase == "outcome" and call.error is None and reply is not None
    if done and isinstance(reply.get("result", {}).get("structuredContent"), dict):
        body = {**reply["result"]["structuredContent"], "warning": UNAUDITED}
        return result(req.id, tool_result(Outcome(body=body)))
    if req.method == "tools/call":
        return result(req.id, tool_result(Outcome(error=ToolError("E_AUDIT"))))
    return error(req.id, INTERNAL_ERROR, "The audit log could not be written")
