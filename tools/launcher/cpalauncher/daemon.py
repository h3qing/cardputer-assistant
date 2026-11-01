"""Keep cpad off the port while the tool works (`cpa pause`), give it back after (`cpa pause 1`).

As in firmware/app/scripts/pre_upload.py, a longer pause already in place is never shortened at the
start. `cpa pause N` sets the pause to N s from now, so resuming is `cpa pause 1`. Without `cpa` on
PATH nothing is paused (the port check in link.py still refuses a held port). Never fatal.
"""

from __future__ import annotations

import json
import math
import shutil
import subprocess
from collections.abc import Callable
from typing import Any

from cpalauncher.verify import daemon_verdict

PAUSE_MIN_S, PAUSE_MAX_S = 600, 86400  # cpad accepts 1..86400
SLOW_BYTES_PER_S = 4096  # well under the 10.7 KB/s measured on 2026-10-20
MARGIN_S = 300
STATUS_EVERY_S = 2.0


def pause_seconds(wait_s: float, size: int) -> int:
    """Long enough for the user to reach the menu and for a slow transfer, within cpad's limits."""
    wanted = math.ceil(wait_s + size / SLOW_BYTES_PER_S + MARGIN_S)
    return max(PAUSE_MIN_S, min(PAUSE_MAX_S, wanted))


class DaemonPause:
    def __init__(self, seconds: int, *, run: Callable[..., Any] = subprocess.run,
                 which: Callable[[str], str | None] = shutil.which, say: Callable[[str], None] = print,
                 stay: bool = False) -> None:  # fmt: skip
        self._seconds, self._run, self._say, self._stay = seconds, run, say, stay
        self._cpa = which("cpa")
        self._active = False

    def __enter__(self) -> DaemonPause:
        self.start()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.resume()

    def start(self) -> None:
        if self._cpa is None:
            self._say("cpa not found on PATH: cpad is not paused (the port check still applies)")
            return
        doc = self.status()
        left = float(doc.get("paused_s") or 0) if doc else 0.0
        self._active = True
        if left >= self._seconds:
            self._say(f"cpad already paused for {left:.0f} s")
            return
        result = self._call("pause", str(self._seconds))
        if result is not None and result.returncode != 0:
            self._say(f"warn: cpa pause exited {result.returncode}")
        elif result is not None:
            self._say(f"cpad paused for {self._seconds} s")

    def resume(self) -> None:
        """`cpa pause 1` once, unless asked to stay paused; a no-op when nothing was paused."""
        if not self._active:
            return
        self._active = False
        if self._stay:
            self._say("cpad stays paused; run `cpa pause 1` when done")
            return
        if self._call("pause", "1") is not None:
            self._say("cpad resumed (cpa pause 1)")

    def status(self) -> dict[str, Any] | None:
        if self._cpa is None:
            return None
        result = self._call("status", "--json")
        try:
            doc = json.loads(result.stdout) if result is not None else None
        except (ValueError, TypeError):
            return None
        return doc if isinstance(doc, dict) else None

    def _call(self, *args: str) -> Any:
        try:
            return self._run([self._cpa, *args], capture_output=True, text=True, timeout=15)
        except (OSError, subprocess.TimeoutExpired) as exc:
            self._say(f"warn: cpa {args[0]} failed ({exc.__class__.__name__})")
            return None


def wait_for_daemon(pause: DaemonPause, clock: Callable[[], float], sleep: Callable[[float], None],
                    expect_fw: str | None, timeout_s: float) -> tuple[bool, str]:  # fmt: skip
    """Poll `cpa status --json` until our firmware is linked under the Launcher, or give up."""
    deadline = clock() + timeout_s
    while True:
        ok, detail = daemon_verdict(pause.status(), expect_fw)
        if ok or clock() >= deadline:
            return ok, detail
        sleep(STATUS_EVERY_S)
