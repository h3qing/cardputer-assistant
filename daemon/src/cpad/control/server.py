"""Control channel (05 §7.9): AF_UNIX socket $CPA_HOME/cpad.sock, mode 0600, one JSON line per request.

Commands: {"cmd":"status"}, {"cmd":"pause","seconds":N} (default 120, 1..86400), {"cmd":"rec"} (v0.3:
recording sessions, ids / paths / states only; v0.4.1 adds project keys, ★ and hidden) and {"cmd":"hook",...}
(one Claude Code hook event from `cpa hook`, checked by agents/claude_code/hooks.parse). No TCP, ever.
docs/23 §1.3: the status and rec replies carry `api`, an integer raised whenever cpa needs a newer cpad, so a cpad
started before an update (its __version__ never changes) is told apart; a reply without it is api 0.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import os
import sys
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any

from cpad.agents.claude_code import hooks
from cpad.protocol.validate import is_int

log = logging.getLogger("cpad.control")
API = 2  # 1: the filing fold (effective projects, rec.filed), docs/23 P1-A; 2: an item handed from a recording
# (origin "inbox") starts in plan mode and reads Voice· on the board, P1-C (cli.filing.HAND_API)
Handler = Callable[[dict[str, Any]], Awaitable[dict[str, Any]]]
PAUSE_DEFAULT_S = 120
PAUSE_MAX_S = 86400
REQUEST_MAX = 1024
# sockaddr_un.sun_path is 104 bytes on macOS and 108 on Linux, including the NUL terminator.
SOCKET_PATH_MAX = 103 if sys.platform == "darwin" else 107


class BadRequest(ValueError):
    """The control request is malformed."""


class SocketPathTooLong(ValueError):
    """$CPA_HOME/cpad.sock does not fit in an AF_UNIX address."""


def check_socket_path(path: Path, max_bytes: int = SOCKET_PATH_MAX) -> None:
    size = len(os.fsencode(path))
    if size > max_bytes:
        raise SocketPathTooLong(
            f"control socket path is {size} bytes, over the {max_bytes}-byte limit for unix sockets: "
            f"{path}. Set CPA_HOME to a shorter directory."
        )


def parse_request(line: bytes) -> dict[str, Any]:
    try:
        doc = json.loads(line)
    except ValueError as exc:
        raise BadRequest("request is not JSON") from exc
    if not isinstance(doc, dict) or doc.get("cmd") not in ("status", "pause", "rec", "hook"):
        raise BadRequest("unknown command")
    if doc["cmd"] == "hook":
        try:
            return {"cmd": "hook", "event": hooks.parse(doc)}
        except (ValueError, TypeError) as exc:
            raise BadRequest(str(exc)) from exc
    if doc["cmd"] in ("status", "rec"):
        return {"cmd": doc["cmd"]}
    seconds = doc.get("seconds", PAUSE_DEFAULT_S)
    if not is_int(seconds) or not 1 <= seconds <= PAUSE_MAX_S:
        raise BadRequest("seconds must be an integer from 1 to 86400")
    return {"cmd": "pause", "seconds": seconds}


class ControlServer:
    def __init__(self, path: Path, handler: Handler):
        self.path = path
        self._handler = handler
        self._server: asyncio.base_events.Server | None = None

    async def start(self) -> None:
        check_socket_path(self.path)
        with contextlib.suppress(FileNotFoundError):
            self.path.unlink()
        old = os.umask(0o177)
        try:
            self._server = await asyncio.start_unix_server(self._serve, path=str(self.path))
        finally:
            os.umask(old)
        os.chmod(self.path, 0o600)

    async def _serve(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            line = await asyncio.wait_for(reader.readline(), 5.0)
            try:
                reply = await self._handler(parse_request(line[:REQUEST_MAX]))
            except BadRequest as exc:
                reply = {"ok": False, "error": str(exc)}
            writer.write(json.dumps(reply, ensure_ascii=False).encode() + b"\n")
            await writer.drain()
        except (TimeoutError, ConnectionError) as exc:
            log.warning("control request dropped", extra={"fields": {"err": type(exc).__name__}})
        finally:
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

    async def close(self) -> None:
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
        with contextlib.suppress(FileNotFoundError):
            self.path.unlink()
