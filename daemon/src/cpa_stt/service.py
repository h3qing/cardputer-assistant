"""An optional LaunchAgent that keeps `cpa-stt watch` running (docs/22 §4.2). Not installed by default.

`cpa-stt service install` writes ~/Library/LaunchAgents/com.cardputer-assistant.cpa-stt.plist (RunAtLoad,
KeepAlive, background) running `python -m cpa_stt watch`, with this terminal's language variables (launchd.lang_env:
what cpa-stt writes for "nothing heard" follows them), and bootstraps it; `uninstall` (and `cpa-stt off`) boots it
out and removes the plist. Written like cpad's service/launchd.py, and like it never boots out or replaces a job
loaded from another HOME's plist (the label is per user, not per HOME).
"""

from __future__ import annotations

import contextlib
import os
import plistlib
import sys
from pathlib import Path
from typing import Any

from cpad.config.files import ensure_private_dir
from cpad.service.launchd import Runner, ServiceError, Uninstalled, job, lang_env, other_plist

LABEL = "com.cardputer-assistant.cpa-stt"


def program() -> list[str]:
    return [sys.executable, "-m", "cpa_stt", "watch"]


def plist_path(agents_dir: Path) -> Path:
    return agents_dir / f"{LABEL}.plist"


def plist_dict(log_dir: Path, cpa_home: str) -> dict[str, Any]:
    return {"Label": LABEL, "ProgramArguments": program(), "RunAtLoad": True, "KeepAlive": True,
            "ProcessType": "Background", "StandardOutPath": str(log_dir / "launchd.out.log"),
            "StandardErrorPath": str(log_dir / "launchd.err.log"),
            "EnvironmentVariables": {"CPA_HOME": cpa_home, **lang_env(os.environ)}}  # fmt: skip


def _domain(uid: int | None) -> str:
    return f"gui/{os.getuid() if uid is None else uid}"


def install(agents_dir: Path, log_dir: Path, cpa_home: str, *, runner: Runner, uid: int | None) -> Path:
    path = plist_path(agents_dir)
    if (other := other_plist(job(runner, f"{_domain(uid)}/{LABEL}"), path)) is not None:
        raise ServiceError(f"a cpa-stt LaunchAgent from another HOME is loaded ({other}); uninstall that one first")
    ensure_private_dir(log_dir)
    agents_dir.mkdir(parents=True, exist_ok=True)
    path.write_bytes(plistlib.dumps(plist_dict(log_dir, cpa_home)))
    os.chmod(path, 0o644)
    with contextlib.suppress(OSError):
        runner(["launchctl", "bootout", f"{_domain(uid)}/{LABEL}"])
    runner(["launchctl", "bootstrap", _domain(uid), str(path)])
    return path


def uninstall(agents_dir: Path, *, runner: Runner, uid: int | None) -> Uninstalled:
    path = plist_path(agents_dir)
    if not path.exists():
        return Uninstalled(False, None)
    other = other_plist(job(runner, f"{_domain(uid)}/{LABEL}"), path)
    if other is None:
        runner(["launchctl", "bootout", f"{_domain(uid)}/{LABEL}"])
    with contextlib.suppress(FileNotFoundError):
        path.unlink()
    return Uninstalled(True, other)
