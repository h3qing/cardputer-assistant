"""The Claude Code settings.json change behind `cpa hooks install|uninstall` (#33).

Each handler is exec form ({"type":"command","command":"/abs/cpa","args":["hook"],"timeout":5}): no shell, so
a path with spaces is safe. A handler is ours when its command is named cpa and its args start with "hook";
uninstall removes exactly those and nothing else. install/uninstall are pure; save() writes atomically
through a symlink, keeps the file mode, refuses when the file changed since it was read, and first
copies the old file into $CPA_HOME/hook-backups (0700 folder, 0600 file), never beside it: a project's
settings.local.json is git-ignored by its exact name, and a copy next to it would not be.
"""

from __future__ import annotations

import hashlib
import json
import os
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cpad.config.files import atomic_write, ensure_private_dir

TIMEOUT_S = 5
NOTIFY = "permission_prompt|idle_prompt|elicitation_dialog|elicitation_url_dialog|auth_success|quota_auto_resume_stale"
DIALOG_TOOLS = "AskUserQuestion|ExitPlanMode"
EVENTS = (("SessionStart", None), ("UserPromptSubmit", None), ("PermissionRequest", "*"), ("PreToolUse", DIALOG_TOOLS),
          ("PostToolUse", DIALOG_TOOLS), ("Notification", NOTIFY), ("Stop", None), ("StopFailure", None),
          ("SessionEnd", None))  # fmt: skip
BACKUP_DIR = "hook-backups"


class SettingsChanged(ValueError):
    """settings.json changed between reading and writing it."""


@dataclass(frozen=True, slots=True)
class Loaded:
    path: Path
    target: Path
    raw: bytes | None
    doc: Mapping[str, Any]
    mode: int | None


def default_path() -> Path:
    return Path(os.environ.get("CLAUDE_CONFIG_DIR") or Path.home() / ".claude") / "settings.json"


def check_cpa(path: str) -> str:
    if not os.path.isabs(path) or os.path.basename(path) != "cpa":
        raise ValueError(f"--cpa must be an absolute path to the cpa executable, not {path}")
    if not (os.path.isfile(path) and os.access(path, os.X_OK)):
        raise ValueError(f"{path} is not an executable file")
    return path


def find_cpa(argv0: str, which: Callable[[str], str | None]) -> str:
    """The cpa that is running now, else the one on PATH."""
    if os.path.basename(argv0) == "cpa":
        return check_cpa(os.path.abspath(argv0))
    found = which("cpa")
    if found is None:
        raise ValueError("cannot find the cpa executable; pass --cpa /absolute/path/to/cpa")
    return check_cpa(os.path.abspath(found))


def handler(cpa: str, home: str | None = None) -> dict[str, Any]:
    args = ["hook", "--home", home] if home else ["hook"]
    return {"type": "command", "command": cpa, "args": args, "timeout": TIMEOUT_S}


def snippet(cpa: str, home: str | None = None) -> dict[str, Any]:
    entry = handler(cpa, home)
    groups = {event: [{**({"matcher": m} if m else {}), "hooks": [entry]}] for event, m in EVENTS}
    return {"hooks": groups}


def is_ours(entry: Any) -> bool:
    if not isinstance(entry, Mapping) or entry.get("type") != "command":
        return False
    command, args = entry.get("command"), entry.get("args")
    named_cpa = isinstance(command, str) and os.path.basename(command) == "cpa"
    return named_cpa and isinstance(args, list) and args[:1] == ["hook"]


def _hooks_of(settings: Any) -> Mapping[str, Any]:
    if not isinstance(settings, Mapping):
        raise ValueError("settings.json must hold a JSON object")
    hooks = settings.get("hooks", {})
    if not isinstance(hooks, Mapping):
        raise ValueError("settings.json: hooks must be an object")
    return hooks


def _strip_groups(groups: list) -> list:
    """Groups without our handlers; a group we emptied is dropped, one that was already empty is kept."""
    kept = []
    for group in groups:
        entries = group.get("hooks") if isinstance(group, Mapping) else None
        if not isinstance(entries, list):
            kept.append(group)
            continue
        mine = [e for e in entries if not is_ours(e)]
        if mine or not entries:
            kept.append({**group, "hooks": mine})
    return kept


def uninstall(settings: Mapping[str, Any]) -> dict[str, Any]:
    hooks = _hooks_of(settings)
    stripped = {k: _strip_groups(v) if isinstance(v, list) else v for k, v in hooks.items()}
    left = {k: v for k, v in stripped.items() if v or not isinstance(hooks[k], list) or not hooks[k]}
    if left == hooks:
        return dict(settings)
    rest = {k: v for k, v in settings.items() if k != "hooks"}
    return {**rest, "hooks": left} if left else rest


def install(settings: Mapping[str, Any], cpa: str, home: str | None = None) -> dict[str, Any]:
    base = uninstall(settings)
    hooks = dict(_hooks_of(base))
    for event, groups in snippet(cpa, home)["hooks"].items():
        existing = hooks.get(event, [])
        if not isinstance(existing, list):
            raise ValueError(f"settings.json: hooks.{event} must be a list")
        hooks = {**hooks, event: [*existing, *groups]}
    return {**base, "hooks": hooks}


def load(path: Path) -> Loaded:
    target = path.resolve()
    try:
        raw = target.read_bytes()
    except FileNotFoundError:
        return Loaded(path, target, None, {}, None)
    try:
        doc = json.loads(raw)
    except ValueError as exc:
        raise ValueError(f"{path} is not valid JSON; fix it by hand first") from exc
    if not isinstance(doc, dict):
        raise ValueError(f"{path} must hold a JSON object")
    return Loaded(path, target, raw, doc, target.stat().st_mode & 0o777)


def _current(target: Path) -> bytes | None:
    try:
        return target.read_bytes()
    except FileNotFoundError:
        return None


def backup_path(backup_dir: Path, path: Path) -> Path:
    """One backup per real settings file: <sha256 of its resolved path, 16 hex>-<its name>."""
    target = path.resolve()
    return backup_dir / f"{hashlib.sha256(os.fsencode(target)).hexdigest()[:16]}-{target.name}"


def save(loaded: Loaded, doc: Mapping[str, Any], backup_dir: Path) -> Path | None:
    """Writes doc; returns the backup path in backup_dir (None when there was no file before)."""
    if _current(loaded.target) != loaded.raw:
        raise SettingsChanged(f"{loaded.path} changed while cpa was editing it; run the command again")
    loaded.target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    backup = None
    if loaded.raw is not None:
        ensure_private_dir(backup_dir)
        backup = backup_path(backup_dir, loaded.target)
        atomic_write(backup, loaded.raw)
    data = (json.dumps(doc, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    atomic_write(loaded.target, data, loaded.mode if loaded.mode is not None else 0o600)
    return backup
