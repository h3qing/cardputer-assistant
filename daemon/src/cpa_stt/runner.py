"""Running the configured command on one WAV (docs/22 §4.3, §4.5). I/O.

Never through a shell: [sandbox-exec, -p, PROFILE, *argv] with {wav} / {offset_ms} filled in. The
environment is only PATH=/usr/bin:/bin, HOME and LANG=en_US.UTF-8 (plus __CF_USER_TEXT_ENCODING when
cpa-stt has it, so CoreFoundation does not read the account's real ~/.CFUserTextEncoding); the working
directory is an empty 0700 temp dir (the only place the sandbox lets it write, besides the Metal cache)
removed afterwards; stdin is closed; at most stdout_cap(seconds) of stdout is read (256 KiB plus 160 B per
second of audio, about ten times what whisper prints), and a run that prints more is a failed run, never a
cut transcript; stderr is dropped unread (it may hold transcript text). A run that outlives its timeout, or
whose tick() says to stop (the claim is gone, cpa-stt is stopping), is killed with its whole process group.
No sandbox-exec, no run.
"""

from __future__ import annotations

import contextlib
import os
import shutil
import signal
import subprocess
import tempfile
import time
from collections.abc import Callable, Iterator, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import IO

from cpa_stt import profile
from cpa_stt.config import OFFSET, WAV
from cpa_stt.parse import STDOUT_MAX
from cpa_stt.scan import wav_header

SANDBOX_EXEC = "/usr/bin/sandbox-exec"
CHUNK = 64 * 1024
POLL_S = 0.2
STDOUT_PER_S = 160


class SandboxMissing(RuntimeError):
    """sandbox-exec is not there: the command is never run unsandboxed."""


@dataclass(frozen=True, slots=True)
class Env:
    sandbox_exec: str
    cache_dir: str | None
    mach: tuple[str, ...]
    home: str


@dataclass(frozen=True, slots=True)
class Outcome:
    exit: int | None
    timed_out: bool
    stdout: bytes
    took_ms: int
    lost: bool = False
    truncated: bool = False

    @property
    def ok(self) -> bool:
        return self.exit == 0 and not (self.timed_out or self.lost or self.truncated)


def fill_argv(argv: Sequence[str], wav: str, offset_ms: int) -> tuple[str, ...]:
    return tuple(wav if a == WAV else str(offset_ms) if a == OFFSET else a for a in argv)


def timeout_for(timeout_s: float, duration_s: float) -> float:
    return max(timeout_s, 30 + 2 * duration_s)


def stdout_cap(duration_s: float) -> int:
    return STDOUT_MAX + STDOUT_PER_S * int(duration_s)


def child_env(home: str) -> dict[str, str]:
    env = {"PATH": "/usr/bin:/bin", "HOME": home, "LANG": "en_US.UTF-8"}
    cf = os.environ.get("__CF_USER_TEXT_ENCODING")  # without it CoreFoundation reads the real ~/.CFUserTextEncoding
    return {**env, "__CF_USER_TEXT_ENCODING": cf} if cf else env


def darwin_cache_dir(read: Callable[[list[str]], str]) -> str | None:
    """`getconf DARWIN_USER_CACHE_DIR`, resolved (the Metal shader cache lives below it)."""
    found = read(["/usr/bin/getconf", "DARWIN_USER_CACHE_DIR"]).strip()
    return os.path.realpath(found) if found else None


@contextlib.contextmanager
def workdir() -> Iterator[Path]:
    path = Path(tempfile.mkdtemp(prefix="cpa-stt-"))
    try:
        yield Path(os.path.realpath(path))
    finally:
        shutil.rmtree(path, ignore_errors=True)


def write_wav(out: Path, segments: Sequence[Path]) -> None:
    """One WAV from contiguous segments (their 44 B headers dropped, a new one written)."""
    data_len = sum(os.path.getsize(p) - 44 for p in segments)
    with open(out, "wb") as dst:
        dst.write(wav_header(data_len))
        for path in segments:
            with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as src:
                src.seek(44)
                shutil.copyfileobj(src, dst, 1 << 20)


def _read(stream: IO[bytes], limit: int) -> tuple[bytes, bool]:
    """(up to `limit` bytes, whether there was more); the rest is read and dropped, so the command never blocks
    on a full pipe."""
    head, total = bytearray(), 0
    while chunk := stream.read(CHUNK):
        head += chunk[: max(0, limit - len(head))]
        total += len(chunk)
    return bytes(head), total > limit


def _wait(proc: subprocess.Popen[bytes], timeout_s: float, tick: Callable[[], bool]) -> str | None:
    """Wait for the command, calling tick() meanwhile; None when it ended, else why it was killed
    ("timeout", or "lost" when tick() returned False)."""
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            proc.wait(timeout=POLL_S)
            return None
        except subprocess.TimeoutExpired:
            pass
        why = "lost" if not tick() else "timeout" if time.monotonic() >= deadline else None
        if why:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
            return why


def run(argv: Sequence[str], wav: Path, offset_ms: int, timeout_s: float, env: Env, wd: Path, *,
        tick: Callable[[], bool] = lambda: True, stdout_max: int = STDOUT_MAX) -> Outcome:  # fmt: skip
    if not os.access(env.sandbox_exec, os.X_OK):
        raise SandboxMissing(env.sandbox_exec)
    text = profile.build(str(wd), env.cache_dir, env.mach)
    cmd = [env.sandbox_exec, "-p", text, *fill_argv(argv, str(wav), offset_ms)]
    start = time.monotonic()
    proc = subprocess.Popen(cmd, cwd=wd, env=child_env(env.home), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, start_new_session=True)  # fmt: skip
    with proc.stdout as stream, ThreadPoolExecutor(max_workers=1) as pool:  # type: ignore[union-attr]
        reading = pool.submit(_read, stream, stdout_max)
        killed = _wait(proc, timeout_s, tick)
        stdout, more = reading.result()
    took = int((time.monotonic() - start) * 1000)
    code = None if killed else proc.returncode
    return Outcome(code, killed == "timeout", stdout, took, lost=killed == "lost", truncated=more)
