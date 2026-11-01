"""$CPA_HOME/logs/mcp-audit.jsonl (docs/15 §3.9): what left the archive, never the content itself.

One line per call: time, pids, the parent process name, the client's self-reported name (recorded,
never trusted), era, method, tool, argument keys, validated ids, counts, bytes, ms, error code, and
for text inputs only their length. A write tool gets two lines: phase "intent" before it runs and
"outcome" after. File 0600 in a 0700 directory, appended under flock, rotated at 1 MiB with five
kept. If a line cannot be written, the caller fails the call (AuditError).
"""

from __future__ import annotations

import fcntl
import json
import os
import subprocess
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad.config.files import FILE_MODE, ensure_private_dir
from cpad.mcp.era import ClientInfo

FILE_NAME = "mcp-audit.jsonl"
MAX_IDS = 10
NAME_MAX = 64


class AuditError(OSError):
    """The audit line could not be written."""


@dataclass(frozen=True, slots=True)
class Proc:
    pid: int
    ppid: int
    parent: str | None


@dataclass(frozen=True, slots=True)
class Call:
    """Everything the audit may know about one call. No field ever holds content."""

    method: str
    era: str | None = None
    client: ClientInfo | None = None
    tool: str | None = None
    args: tuple[str, ...] = ()
    ids: tuple[str, ...] = ()
    n: int | None = None
    bytes: int = 0
    error: str | int | None = None
    sizes: Mapping[str, int] = field(default_factory=lambda: MappingProxyType({}))
    phase: str | None = None  # write tools only: "intent" (before it runs) or "outcome"


def local_iso(ts: float) -> str:
    return datetime.fromtimestamp(ts).astimezone().isoformat(timespec="milliseconds")


def _client(info: ClientInfo | None) -> dict[str, str] | None:
    return None if info is None else {"name": info.name, "version": info.version}


def record(call: Call, proc: Proc, at: float, ms: float) -> dict[str, Any]:
    rec = {"at": local_iso(at), "pid": proc.pid, "ppid": proc.ppid, "parent": proc.parent,
           "client": _client(call.client), "era": call.era, "method": call.method, "tool": call.tool,
           "args": sorted(call.args), "ids": list(call.ids[:MAX_IDS]), "n": call.n, "bytes": call.bytes,
           "ms": round(ms), "error": call.error}  # fmt: skip
    return {**rec, **({"phase": call.phase} if call.phase else {}), **call.sizes}


def start_record(proc: Proc, at: float, version: str, writes: frozenset[str], disabled: frozenset[str]) -> dict:
    return {"at": local_iso(at), "event": "start", "pid": proc.pid, "ppid": proc.ppid, "parent": proc.parent,
            "version": version, "write_tools": sorted(writes), "disabled_tools": sorted(disabled)}  # fmt: skip


def stop_record(proc: Proc, at: float, calls: int) -> dict[str, Any]:
    return {"at": local_iso(at), "event": "stop", "pid": proc.pid, "calls": calls}


def proc_info(runner: Callable[..., subprocess.CompletedProcess] = subprocess.run) -> Proc:
    """Our pid, the parent pid and the parent's process name (asked once, at start-up)."""
    ppid = os.getppid()
    try:
        out = runner(["ps", "-o", "comm=", "-p", str(ppid)], capture_output=True, text=True, timeout=2, check=False)
        name = os.path.basename(out.stdout.strip().splitlines()[0].strip()) if out.stdout.strip() else None
    except (OSError, subprocess.SubprocessError, IndexError):
        name = None
    return Proc(os.getpid(), ppid, name[:NAME_MAX] if name else None)


@dataclass(frozen=True, slots=True)
class AuditLog:
    path: Path
    max_bytes: int = 1 << 20
    keep: int = 5

    def append(self, rec: Mapping[str, Any]) -> None:
        line = (json.dumps(rec, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8", "replace")
        try:
            ensure_private_dir(self.path.parent)
            self._append(line)
        except OSError as exc:
            raise AuditError("the audit log could not be written") from exc

    def _rotate(self) -> None:
        for n in range(self.keep - 1, 0, -1):
            older = self.path.with_name(f"{self.path.name}.{n}")
            if older.exists():
                os.replace(older, self.path.with_name(f"{self.path.name}.{n + 1}"))
        os.replace(self.path, self.path.with_name(f"{self.path.name}.1"))

    def _current(self, fd: int) -> bool:
        try:
            return os.fstat(fd).st_ino == os.stat(self.path, follow_symlinks=False).st_ino
        except FileNotFoundError:
            return False

    def _append(self, line: bytes) -> None:
        for _ in range(4):
            fd = os.open(self.path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, FILE_MODE)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX)
                if not self._current(fd):
                    continue  # another server rotated it while we waited for the lock
                size = os.fstat(fd).st_size
                if size and size + len(line) > self.max_bytes:
                    self._rotate()
                    continue
                os.fchmod(fd, FILE_MODE)
                os.write(fd, line)
                os.fsync(fd)
                return
            finally:
                os.close(fd)
        raise AuditError("the audit log kept rotating")


def _lines(path: Path) -> list[bytes]:
    try:
        return path.read_bytes().splitlines()
    except OSError:
        return []


def _parse(line: bytes) -> dict[str, Any] | None:
    try:
        doc = json.loads(line)
    except ValueError:
        return None
    return doc if isinstance(doc, dict) else None


def tail(path: Path, n: int) -> tuple[dict[str, Any], ...]:
    """The last n readable records, oldest first, from the current file and the newest rotated one."""
    raw = _lines(path.with_name(f"{path.name}.1")) + _lines(path)
    parsed = (doc for doc in map(_parse, raw) if doc is not None)
    return tuple(parsed)[-n:] if n > 0 else ()
