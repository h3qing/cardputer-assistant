"""PlatformIO pre script (06 WP4): before an upload, ask the daemon to let go of the
serial port with `cpa pause 120`, so esptool and the daemon never fight over it (R6).
`cpa pause N` sets the pause to N s from now, it does not extend it: when the daemon is
already paused for at least that long (08 §4: an agent ran `cpa pause 5400` first), the
pause is left alone, so the real cpad cannot reconnect in the middle of a test run.
Skipped when cpa is not installed; never fails the upload.
"""

from __future__ import annotations

import json
import shutil
import subprocess

PAUSE_SECONDS = 120


def paused_for(cpa: str, run=subprocess.run) -> float:
    """Seconds the daemon stays paused (`cpa status --json`); 0 when unknown."""
    try:
        result = run(
            [cpa, "status", "--json"], timeout=10, capture_output=True, text=True
        )
        return float(json.loads(result.stdout).get("paused_s") or 0)
    except (OSError, subprocess.TimeoutExpired, ValueError, TypeError, AttributeError):
        return 0.0


def pause_daemon(
    targets, which=shutil.which, run=subprocess.run, seconds: int = PAUSE_SECONDS
) -> str:
    if "upload" not in targets:
        return "skip: no upload"
    cpa = which("cpa")
    if not cpa:
        return "skip: cpa not found"
    left = paused_for(cpa, run)
    if left >= seconds:
        return f"skip: already paused for {left:.0f} s"
    try:
        result = run(
            [cpa, "pause", str(seconds)], timeout=10, capture_output=True, text=True
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"warn: cpa pause failed ({exc.__class__.__name__})"
    return (
        "paused"
        if result.returncode == 0
        else f"warn: cpa pause exited {result.returncode}"
    )


try:  # as a PlatformIO extra script
    Import("env")
    from SCons.Script import COMMAND_LINE_TARGETS

    print(f"pre_upload: {pause_daemon(COMMAND_LINE_TARGETS)}")
except NameError:  # imported by pytest
    pass
