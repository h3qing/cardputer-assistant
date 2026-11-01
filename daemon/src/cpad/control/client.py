"""Client side of cpad.sock, used by the cpa CLI (synchronous)."""

from __future__ import annotations

import json
import socket
from pathlib import Path
from typing import Any


class DaemonNotRunning(RuntimeError):
    """Nothing listens on cpad.sock."""


def request(path: Path, payload: dict[str, Any], timeout: float = 5.0) -> dict[str, Any]:
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect(str(path))
    except (FileNotFoundError, ConnectionRefusedError) as exc:
        sock.close()
        raise DaemonNotRunning("cpad is not running") from exc
    with sock, sock.makefile("rwb") as stream:
        stream.write(json.dumps(payload).encode() + b"\n")
        stream.flush()
        line = stream.readline()
    if not line:
        raise DaemonNotRunning("cpad closed the connection")
    return json.loads(line)
