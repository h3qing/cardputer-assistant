"""The HIL rig (I/O shell): a temporary $CPA_HOME, notes_dir and project, fake-claude, and the real
cpad Daemon in this process on the real serial port, wrapped by the tap.

Nothing here touches the user's real $CPA_HOME, ~/.claude or LaunchAgents: config.json points
claude_bin at daemon/tests/fixtures/bin/fake-claude (or, for real_claude_smoke only, at the real
claude), and every `cpa` call runs with CPA_HOME set to the temporary home.
"""

from __future__ import annotations

import asyncio
import contextlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from cpad.app import Daemon
from cpad.config.settings import Config, Paths, load_config, replace_config, save_config, secure_home
from cpad.device.link import DeviceLink
from cpad.device.port_finder import list_ports
from serial.tools.list_ports import comports

from harness.fakeclaude import BIN, FakeClaudeDir
from harness.notes import NoteLine, read_notes
from harness.tap import Tap

PROJECT_KEY = "cpa"
PROJECT_NAME = "Cardputer 助手"
CARDPUTER = (0x303A, 0x1001)


async def until(predicate: Callable[[], Any], timeout: float, interval: float = 0.05, what: str = "condition"):
    """Poll predicate until it returns something truthy; TimeoutError names what never happened."""
    deadline = time.monotonic() + timeout
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise TimeoutError(f"{what} not met within {timeout:g} s")
        await asyncio.sleep(interval)


def port_holders() -> dict[str, str]:
    """Cardputer ports some other process has open (lsof), as {port: pids}."""
    held = {}
    for p in comports():
        if (p.vid, p.pid) != CARDPUTER:
            continue
        done = subprocess.run(["lsof", "-t", p.device], capture_output=True, text=True, check=False)
        if done.stdout.strip():
            held[p.device] = done.stdout.strip().replace("\n", ",")
    return held


@dataclass
class Rig:
    tmp: Path
    paths: Paths
    notes_dir: Path
    project_root: Path
    fake: FakeClaudeDir | None
    tap: Tap = field(default_factory=Tap)
    daemon: Daemon | None = None
    task: asyncio.Task | None = None
    stop_event: asyncio.Event | None = None

    # --- lifecycle ----------------------------------------------------------------------------

    async def start(self) -> None:
        self.stop_event = asyncio.Event()
        self.daemon = Daemon(self.paths, port_lister=list_ports, serial_factory=self.tap.factory())
        self.task = asyncio.create_task(self.daemon.run(self.stop_event))

    async def halt(self) -> None:
        if self.task is not None and self.stop_event is not None:
            self.stop_event.set()
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self.task, 15)
            self.task = None

    async def close(self, keep: bool) -> None:
        await self.halt()
        if not keep:
            shutil.rmtree(self.tmp, ignore_errors=True)

    def crashed(self) -> BaseException | None:
        if self.task is not None and self.task.done() and not self.task.cancelled():
            return self.task.exception()
        return None

    # --- link ---------------------------------------------------------------------------------

    @property
    def link(self) -> DeviceLink | None:
        link = self.daemon.link if self.daemon else None
        return link if link is not None and link.ready else None

    async def ready(self, timeout: float = 20.0) -> DeviceLink:
        return await until(lambda: self.link, timeout, what="daemon link Ready")

    async def released(self, timeout: float = 5.0) -> None:
        await until(lambda: self.daemon.link is None, timeout, what="serial port released")

    # --- cpa ----------------------------------------------------------------------------------

    def env(self) -> dict[str, str]:
        return {**os.environ, "CPA_HOME": str(self.paths.home)}

    async def cpa(self, *args: str) -> tuple[int, str, str]:
        proc = await asyncio.create_subprocess_exec(
            sys.executable, "-m", "cpad", *args, env=self.env(),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        )  # fmt: skip
        out, err = await asyncio.wait_for(proc.communicate(), 30)
        return proc.returncode or 0, out.decode(), err.decode()

    async def pause(self, seconds: int) -> str:
        rc, out, err = await self.cpa("pause", str(seconds))
        if rc != 0 or "paused for" not in out:
            raise RuntimeError(f"cpa pause {seconds} failed: rc={rc} {out.strip()} {err.strip()}")
        return out.strip()

    async def resume(self) -> str:
        """`cpa pause 1`: the pause ends a second from now and the daemon reconnects."""
        return await self.pause(1)

    # --- files --------------------------------------------------------------------------------

    def events(self) -> list[dict[str, Any]]:
        path = self.paths.events
        lines = path.read_text(encoding="utf-8").splitlines() if path.exists() else []
        return [json.loads(line) for line in lines if line.strip()]

    def notes(self) -> tuple[NoteLine, ...]:
        return read_notes(self.notes_dir)

    def set_config(self, **changes: Any) -> None:
        save_config(self.paths.config, replace_config(load_config(self.paths.config), **changes))


def _cpa_sync(home: Path, *args: str) -> None:
    env = {**os.environ, "CPA_HOME": str(home)}
    done = subprocess.run([sys.executable, "-m", "cpad", *args], env=env, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise RuntimeError(f"cpa {' '.join(args[:2])} failed: {done.stderr.strip()}")


def create_rig(*, claude_bin: str | None = None, project_root: Path | None = None, key: str = PROJECT_KEY) -> Rig:
    """claude_bin None = fake-claude (every automated scenario); a path = the real CLI (smoke only)."""
    tmp = Path(tempfile.mkdtemp(prefix="cpa-hil-")).resolve()
    paths = Paths.at(tmp / "home")
    secure_home(paths)
    notes_dir = tmp / "notes"
    root = Path(project_root) if project_root else tmp / "projects" / key
    root.mkdir(parents=True, exist_ok=True)
    fake = FakeClaudeDir.create(tmp / "fake-claude") if claude_bin is None else None
    env = fake.env(os.environ.get("PATH", "/usr/bin:/bin")) if fake else {}
    config = Config(claude_bin=claude_bin or str(BIN), notes_dir=str(notes_dir), spawn_env=env)
    save_config(paths.config, config)
    _cpa_sync(paths.home, "project", "add", key, str(root), "--slot", "1", "--name", PROJECT_NAME)
    os.environ["CPA_HOME"] = str(paths.home)  # anything that reads $CPA_HOME in this process stays in tmp
    return Rig(tmp, paths, notes_dir, Path(os.path.realpath(root)), fake)
