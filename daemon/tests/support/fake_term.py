"""A fake terminal and a fake afplay for cpa filing triage (cli.triage): keys in, lines out; no process started."""

from __future__ import annotations

import os
from collections.abc import Callable


class FakeTerm:
    """Keys in order (a callable is run between two keys, to change the log meanwhile); "eof" once they run out."""

    def __init__(self, keys: list[str | Callable[[], None]]) -> None:
        self._keys = iter(keys)
        self.lines: list[str] = []

    def key(self) -> str:
        for k in self._keys:
            if not callable(k):
                return k
            k()
        return "eof"

    def say(self, line: str) -> None:
        self.lines.append(line)

    def drop(self) -> None:
        """Keys here are never typed ahead of the screen: nothing to throw away."""


class FakeProc:
    def __init__(self, exit_code: int | None = None) -> None:
        self.stopped = False
        self.exit_code = exit_code  # set: it already ended by itself with this code

    def poll(self) -> int | None:
        return 0 if self.stopped else self.exit_code

    def kill(self) -> None:
        self.stopped = True

    def wait(self, timeout: float | None = None) -> int:
        return 0


class FakeSpawn:
    """Stands in for subprocess.Popen: records argv, the keywords and the inode behind the passed descriptor."""

    def __init__(self, exit_code: int | None = None) -> None:
        self.calls: list[tuple[list[str], dict, int]] = []
        self.procs: list[FakeProc] = []
        self.exit_code = exit_code

    def __call__(self, argv: list[str], **kw) -> FakeProc:
        self.calls.append((argv, kw, os.fstat(kw["pass_fds"][0]).st_ino))
        self.procs.append(FakeProc(self.exit_code))
        return self.procs[-1]
