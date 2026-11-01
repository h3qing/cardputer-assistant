"""`cpa-sim serve` as a child process: start it, learn its pty, send it stdin steps, wait for its events.

The binary is firmware/sim/.pio/build/sim/cpa-sim (`pio run -d firmware/sim`), or $CPA_SIM_BIN.
Its stdout is one event per line (firmware/sim/src/serve.h): `pty <path>` first, then `shot <file>
<module> <backlight>`, `usb on|off`, `bat <mv>`, `reboot <boot>`, `rx_dropped <bytes>`, `error <message>`
and `bye`.
"""

from __future__ import annotations

import os
import queue
import subprocess
import threading
import time
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BIN_REL = Path("firmware/sim/.pio/build/sim/cpa-sim")
DEFAULT_BIN = REPO / BIN_REL
# What cpa-sim is built from (firmware/sim/platformio.ini): a newer file here means a stale binary.
SIM_SOURCES = (
    "firmware/sim/lib", "firmware/sim/src", "firmware/sim/platformio.ini", "firmware/sim/prog_name.py",
    "firmware/app/lib", "firmware/app/modules", "firmware/app/src", "firmware/app/test/ui/support",
    "firmware/app/test/core/support", "firmware/app/platformio.ini",
)  # fmt: skip
NOT_BUILT = ("firmware/app/lib/hal",)  # device only
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".ini", ".json", ".py"}


class SimError(RuntimeError):
    """The simulator refused a step, exited, or did not answer in time."""


@dataclass(frozen=True, slots=True)
class Shot:
    path: Path
    module: str
    backlight: str


@dataclass(frozen=True, slots=True)
class SimProblem:
    message: str
    missing: bool  # not built at all; otherwise built before a change to its sources


def sim_bin(repo: Path = REPO, env: Mapping[str, str] | None = None) -> Path:
    env = os.environ if env is None else env
    return Path(env.get("CPA_SIM_BIN") or repo / BIN_REL)


def _sources(repo: Path) -> Iterator[Path]:
    skip = [repo / rel for rel in NOT_BUILT]
    for rel in SIM_SOURCES:
        root = repo / rel
        paths = [root] if root.is_file() else root.rglob("*")
        for p in paths:
            if p.suffix in SOURCE_SUFFIXES and p.is_file() and not any(p.is_relative_to(d) for d in skip):
                yield p


def stale_against(binary: Path, repo: Path = REPO) -> Path | None:
    """A source file changed after `binary` was built, or None."""
    built = binary.stat().st_mtime
    return next((p for p in _sources(repo) if p.stat().st_mtime > built), None)


def sim_problem(repo: Path = REPO, env: Mapping[str, str] | None = None) -> SimProblem | None:
    """Why the tests cannot use cpa-sim, or None. $CPA_SIM_BIN (another build) is only checked to exist."""
    env = os.environ if env is None else env
    exe = sim_bin(repo, env)
    if not exe.exists():
        return SimProblem(f"{exe} not found: build it with `pio run -d firmware/sim`", missing=True)
    newer = None if env.get("CPA_SIM_BIN") else stale_against(exe, repo)
    if newer is None:
        return None
    return SimProblem(f"{exe} is older than {newer.relative_to(repo)}: rebuild it with `pio run -d firmware/sim`",
                      missing=False)  # fmt: skip


def sim_check(repo: Path = REPO, env: Mapping[str, str] | None = None) -> tuple[str, str]:
    """("run", ""), or ("skip" | "fail", why): a missing build is skipped unless CPA_SIM_REQUIRED=1 (CI);
    a stale one always fails, so the tests never pass on an old binary."""
    env = os.environ if env is None else env
    problem = sim_problem(repo, env)
    if problem is None:
        return "run", ""
    if problem.missing and env.get("CPA_SIM_REQUIRED") != "1":
        return "skip", problem.message
    return "fail", problem.message


class SimServe:
    def __init__(self, proc: subprocess.Popen, lines: queue.Queue):
        self._proc, self._lines = proc, lines
        self.events: list[str] = []  # every line read so far
        self.path = self.expect("pty ", 10.0)[len("pty ") :]

    @classmethod
    def start(cls, shots: Path, args: Sequence[str] = (), binary: Path | None = None) -> SimServe:
        exe = Path(binary) if binary else sim_bin()
        if not exe.exists():
            raise SimError(f"{exe} not found: build it with `pio run -d firmware/sim`")
        cmd = [str(exe), "serve", "--shots", str(shots), *args]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        lines: queue.Queue = queue.Queue()
        threading.Thread(target=_pump, args=(proc, lines), daemon=True).start()
        try:
            return cls(proc, lines)
        except BaseException:
            proc.kill()
            raise

    def send(self, *steps: str) -> None:
        if self._proc.stdin is None or self._proc.poll() is not None:
            raise SimError("cpa-sim serve is not running")
        self._proc.stdin.write("".join(f"{s}\n" for s in steps))
        self._proc.stdin.flush()

    def expect(self, prefix: str, timeout: float) -> str:
        """The next line starting with prefix; an `error` line or the end of the process fails."""
        deadline = time.monotonic() + timeout
        while True:
            try:
                line = self._lines.get(timeout=max(0.0, deadline - time.monotonic()))
            except queue.Empty:
                raise SimError(f"no `{prefix.strip()}` from cpa-sim within {timeout:g} s") from None
            if line is None:
                raise SimError(f"cpa-sim exited (status {self._proc.wait()}) before `{prefix.strip()}`")
            self.events.append(line)
            if line.startswith(prefix):
                return line
            if line.startswith("error "):
                raise SimError(f"cpa-sim: {line}")

    def shot(self, name: str, timeout: float = 5.0) -> Shot:
        self.send(f"shot {name}")
        _, path, module, backlight = self.expect("shot ", timeout).split(" ", 3)
        return Shot(Path(path), module, backlight)

    def stop(self, timeout: float = 5.0) -> int:
        if self._proc.poll() is None:
            try:
                self.send("quit")
                return self._proc.wait(timeout)
            except (SimError, OSError, subprocess.TimeoutExpired):
                self._proc.kill()
        return self._proc.wait()


def _pump(proc: subprocess.Popen, lines: queue.Queue) -> None:
    assert proc.stdout is not None
    for line in proc.stdout:
        lines.put(line.rstrip("\n"))
    lines.put(None)
