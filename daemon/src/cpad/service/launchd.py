"""LaunchAgent for cpad (05 §7.9, US-15; plist idea adapted from cc-buddy-bridge's _service_launchd.py, MIT).

install: capture `zsh -lic env` once and keep only PATH, HOME, CPA_LANG, LANG, LC_*, SHELL and USER (never
secrets) for spawn_env; write ~/Library/LaunchAgents/<label>.plist with RunAtLoad + KeepAlive; bootstrap it. The plist
gives cpad the login shell's CPA_LANG, LC_ALL, LC_MESSAGES and LANG (lang_env): launchd gives it none, and cpad
writes notes in that language (cpad.i18n).
An upgrade waits until the old job is gone first (a bootstrap while it is still loaded fails or is undone, #82),
and only a running job with a new pid (launchctl print) counts as started; one more try, then ServiceError.
"""

from __future__ import annotations

import contextlib
import os
import plistlib
import re
import subprocess
import time
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from types import MappingProxyType
from typing import Any

from cpad import i18n

LABEL = "com.cardputer-assistant.cpad"
ENV_KEYS = frozenset({"PATH", "HOME", "SHELL", "USER", *i18n.KEYS})  # and every other LC_*
ENV_LINE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(.*)\Z")
STATE = re.compile(r"^\tstate = (.+?)\s*$", re.MULTILINE)  # top level only: nested blocks have their own state
PID = re.compile(r"^\tpid = (\d+)\s*$", re.MULTILINE)
WAIT_S = 10.0  # for the old job to go, and again for the new one to run
POLL_S = 0.2


@dataclass(frozen=True, slots=True)
class Completed:
    returncode: int
    stdout: str


def run(argv: Sequence[str], **kwargs: Any) -> Completed:
    done = subprocess.run(list(argv), capture_output=True, text=True, check=False, timeout=15, **kwargs)
    return Completed(done.returncode, done.stdout)


Runner = Callable[..., Completed]


class ServiceError(RuntimeError):
    """cpad's LaunchAgent could not be (re)started; the message says what was seen."""


@dataclass(frozen=True, slots=True)
class Clock:
    sleep: Callable[[float], None] = time.sleep
    now: Callable[[], float] = time.monotonic


REAL_CLOCK = Clock()


@dataclass(frozen=True, slots=True)
class Job:
    loaded: bool
    pid: int | None  # only when launchctl print says state = running


@dataclass(frozen=True, slots=True)
class Installed:
    path: Path
    pid: int


def job(runner: Runner, target: str) -> Job:
    done = runner(["launchctl", "print", target])
    if done.returncode != 0:
        return Job(False, None)
    state, pid = STATE.search(done.stdout), PID.search(done.stdout)
    running = state is not None and state.group(1) == "running"
    return Job(True, int(pid.group(1)) if running and pid else None)


def _poll[T](probe: Callable[[], T | None], clock: Clock, timeout: float) -> T | None:
    deadline = clock.now() + timeout
    while (found := probe()) is None and clock.now() < deadline:
        clock.sleep(POLL_S)
    return found


def default_agents_dir() -> Path:
    return Path.home() / "Library" / "LaunchAgents"


def parse_env(output: str) -> dict[str, str]:
    pairs = (ENV_LINE.match(line) for line in output.splitlines())
    return {m.group(1): m.group(2) for m in pairs if m and (m.group(1) in ENV_KEYS or m.group(1).startswith("LC_"))}


def lang_env(env: Mapping[str, str]) -> dict[str, str]:
    """The variables that decide the language (cpad.i18n), those set and not empty."""
    return {key: env[key] for key in i18n.KEYS if env.get(key)}


def capture_login_env(runner: Runner = run) -> dict[str, str]:
    done = runner(["/bin/zsh", "-lic", "env"], stdin=subprocess.DEVNULL)
    return parse_env(done.stdout) if done.returncode == 0 else {}


def plist_dict(
    program: Sequence[str], home: Path, cpa_home_env: str | None, env: Mapping[str, str] = MappingProxyType({})
) -> dict[str, Any]:
    doc: dict[str, Any] = {
        "Label": LABEL,
        "ProgramArguments": list(program),
        "RunAtLoad": True,
        "KeepAlive": True,
        "ProcessType": "Background",
        "StandardOutPath": str(home / "logs" / "launchd.out.log"),
        "StandardErrorPath": str(home / "logs" / "launchd.err.log"),
    }
    variables = {**({"CPA_HOME": cpa_home_env} if cpa_home_env else {}), **env}
    return {**doc, "EnvironmentVariables": variables} if variables else doc


def plist_path(agents_dir: Path) -> Path:
    return agents_dir / f"{LABEL}.plist"


def install(
    program: Sequence[str], home: Path, agents_dir: Path, *, runner: Runner = run, uid: int | None = None,
    cpa_home_env: str | None = None, clock: Clock = REAL_CLOCK, env: Mapping[str, str] = MappingProxyType({}),
) -> Installed:  # fmt: skip
    agents_dir.mkdir(parents=True, exist_ok=True)
    path = plist_path(agents_dir)
    path.write_bytes(plistlib.dumps(plist_dict(program, home, cpa_home_env, env)))
    os.chmod(path, 0o644)
    domain = f"gui/{os.getuid() if uid is None else uid}"
    target = f"{domain}/{LABEL}"
    old = job(runner, target)
    for attempt in range(2):
        if attempt or old.loaded:
            _stop(runner, target, clock)
        runner(["launchctl", "bootstrap", domain, str(path)])
        pid = _poll(lambda: _new_pid(runner, target, old.pid), clock, WAIT_S)
        if pid is not None:
            return Installed(path, pid)
    raise ServiceError(f"cpad did not start within {WAIT_S:g} s, twice (see: launchctl print {target})")


def _stop(runner: Runner, target: str, clock: Clock) -> None:
    with contextlib.suppress(OSError):
        runner(["launchctl", "bootout", target])
    if _poll(lambda: True if not job(runner, target).loaded else None, clock, WAIT_S) is None:
        raise ServiceError(f"the old cpad did not stop within {WAIT_S:g} s (see: launchctl print {target})")


def _new_pid(runner: Runner, target: str, old_pid: int | None) -> int | None:
    pid = job(runner, target).pid
    return pid if pid is not None and pid != old_pid else None


def uninstall(agents_dir: Path, *, runner: Runner = run, uid: int | None = None) -> bool:
    path = plist_path(agents_dir)
    runner(["launchctl", "bootout", f"gui/{os.getuid() if uid is None else uid}/{LABEL}"])
    existed = path.exists()
    with contextlib.suppress(FileNotFoundError):
        path.unlink()
    return existed
