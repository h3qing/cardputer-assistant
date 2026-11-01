"""The stdio transport of cpa mcp (docs/15 §3.2): no port, one JSON-RPC message per line.

At start fd 1 is moved aside for the protocol and pointed at stderr, so a stray print can never
corrupt the channel. Lines over LINE_MAX are refused and skipped. Requests are handled one at a
time, in order; notifications are ignored. Each call's audit line is written before its reply: if
it cannot be, the reply is replaced by an error and no data leaves. A write tool's intent line is
written before it runs, so a write never happens unaudited. End of input exits with 0.
"""

from __future__ import annotations

import os
import sys
import time
from collections.abc import Callable, Iterator
from dataclasses import replace
from typing import BinaryIO, Protocol

from cpad import __version__
from cpad.archive.fs import Root
from cpad.config.files import ensure_private_dir
from cpad.config.settings import Paths, load_config
from cpad.mcp import audit as audit_log
from cpad.mcp import backend, catalog, dispatch, jsonrpc
from cpad.mcp import config as mcp_config
from cpad.mcp.audit import AuditError, Proc
from cpad.mcp.config import McpConfig
from cpad.mcp.dispatch import Env, State
from cpad.mcp.jsonrpc import LINE_MAX, Notification, Request

CHUNK = 1 << 16


class Audit(Protocol):
    def append(self, rec: dict) -> None: ...


def _discard(inp: BinaryIO) -> None:
    while (chunk := inp.readline(CHUNK)) and not chunk.endswith(b"\n"):
        pass


def lines(inp: BinaryIO) -> Iterator[bytes | None]:
    """Each line without its line break; None for a line over LINE_MAX (whose rest is skipped)."""
    while raw := inp.readline(LINE_MAX + 1):
        if len(raw) > LINE_MAX and not raw.endswith(b"\n"):
            _discard(inp)
            yield None
            continue
        yield raw.rstrip(b"\r\n")


def _write(out: BinaryIO, data: bytes) -> bool:
    try:
        out.write(data)
        out.flush()
    except OSError:  # the client went away (BrokenPipeError)
        return False
    return True


def _answer(state: State, req: Request, env: Env, audit: Audit, proc: Proc,
            clock: Callable[[], float]) -> tuple[State, bytes]:  # fmt: skip
    started = time.monotonic()
    state, reply, call = dispatch.handle(state, req, env, clock())
    data = jsonrpc.encode(reply)
    if call is None:
        return state, data
    try:
        audit.append(
            audit_log.record(replace(call, bytes=len(data)), proc, clock(), (time.monotonic() - started) * 1000)
        )
    except AuditError:
        return state, jsonrpc.encode(dispatch.audit_failed(req, reply, call))
    return state, data


def _reply(state: State, raw: bytes | None, env: Env, audit: Audit, proc: Proc,
           clock: Callable[[], float]) -> tuple[State, bytes | None]:  # fmt: skip
    if raw is None:
        return state, jsonrpc.encode(jsonrpc.error(None, jsonrpc.INVALID_REQUEST, "Message too large"))
    msg = jsonrpc.parse(raw) if raw.strip() else None
    if msg is None or isinstance(msg, Notification):
        return state, None
    if isinstance(msg, dict):
        return state, jsonrpc.encode(msg)
    return _answer(state, msg, env, audit, proc, clock)


def serve(inp: BinaryIO, out: BinaryIO, env: Env, audit: Audit, proc: Proc, cfg: McpConfig,
          clock: Callable[[], float] = time.time) -> int:  # fmt: skip
    try:
        audit.append(audit_log.start_record(proc, clock(), __version__, cfg.write_tools, cfg.disabled_tools))
    except AuditError:
        print("cpa mcp: the audit log cannot be written; not starting", file=sys.stderr)
        return 1
    state = State()
    env = replace(env, intent=lambda call: audit.append(audit_log.record(call, proc, clock(), 0)))
    for raw in lines(inp):
        state, data = _reply(state, raw, env, audit, proc, clock)
        if data is not None and not _write(out, data):
            break
    try:
        audit.append(audit_log.stop_record(proc, clock(), state.calls))
    except AuditError:
        pass
    return 0


def _protocol_out() -> BinaryIO:
    """A private handle on the real stdout; fd 1 and sys.stdout now go to stderr."""
    sys.stdout.flush()
    fd = os.dup(1)
    os.dup2(2, 1)
    sys.stdout = sys.stderr
    return os.fdopen(fd, "wb")


def run(paths: Paths) -> int:
    cfg = mcp_config.load(paths.home / mcp_config.FILE_NAME)
    root = Root.at(load_config(paths.config).notes_path())
    env = Env(ctx=backend.context(paths, root, writes=bool(cfg.write_tools)), tools=catalog.visible(cfg))
    ensure_private_dir(paths.home)
    audit = audit_log.AuditLog(paths.logs / audit_log.FILE_NAME)
    return serve(sys.stdin.buffer, _protocol_out(), env, audit, audit_log.proc_info(), cfg)
