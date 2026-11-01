"""Run a CLI as an argv list (never through a shell), with a timeout (05 §7.1)."""

from __future__ import annotations

import asyncio
import contextlib
from collections.abc import Mapping, Sequence
from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ProcResult:
    rc: int | None
    stdout: str
    stderr: str
    timed_out: bool


async def run(
    argv: Sequence[str], *, cwd: str | None = None, env: Mapping[str, str] | None = None, timeout: float
) -> ProcResult:
    try:
        proc = await asyncio.create_subprocess_exec(
            *argv,
            cwd=cwd,
            env=dict(env) if env is not None else None,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
    except OSError as exc:
        return ProcResult(None, "", type(exc).__name__, False)
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout)
    except TimeoutError:
        with contextlib.suppress(ProcessLookupError):
            proc.kill()
        await proc.wait()
        return ProcResult(None, "", "", True)
    return ProcResult(proc.returncode, out.decode("utf-8", "replace"), err.decode("utf-8", "replace"), False)
