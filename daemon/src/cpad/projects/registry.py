"""projects.json: {"version":1,"projects":[{key,name,slot,root,spawn_permission_mode}]} (05 §7.1, §7.6, US-05).

Rules: key ^[a-z0-9][a-z0-9-]{0,11}$ and unique; slot 1..9 and unique; name 1..24 UTF-8 bytes, no control
characters (default = key); root an absolute path, stored as realpath when added (git is NOT required), or null:
a project is a named group first (docs/25 §8), and only a linked one (a folder) gets hooks, cwd matching and spawns;
spawn_permission_mode defaults to plan and bypassPermissions makes the whole file fail to load.
A linked root that no longer exists still loads, with ok=False (US E11); an unlinked project is ok.
"""

from __future__ import annotations

import os
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cpad.config.files import read_json, write_json
from cpad.protocol.limits import LIMITS
from cpad.protocol.validate import NO_CONTROL, is_int, matches, utf8_len

MODES = ("plan", "manual", "acceptEdits", "dontAsk", "auto")
FORBIDDEN_MODE = "bypassPermissions"
DEFAULT_MODE = "plan"


class ProjectsError(ValueError):
    """projects.json or a project argument is invalid; nothing is written. code / field: the filing/v1 error."""

    def __init__(self, message: str, field: str | None = None, code: str = "E_BAD_ARG") -> None:
        super().__init__(message)
        self.field = field
        self.code = code


@dataclass(frozen=True, slots=True)
class Project:
    key: str
    name: str
    slot: int
    root: str | None
    mode: str = DEFAULT_MODE
    ok: bool = True

    @property
    def linked(self) -> bool:
        return self.root is not None


def _check_mode(mode: Any) -> str:
    if mode is None:
        return DEFAULT_MODE
    if mode == FORBIDDEN_MODE:
        raise ProjectsError("spawn_permission_mode bypassPermissions is never allowed", "mode")
    if mode not in MODES:
        raise ProjectsError(f"spawn_permission_mode must be one of {', '.join(MODES)}", "mode")
    return mode


def _check_fields(key: Any, name: Any, slot: Any) -> None:
    if not matches(LIMITS["project_key_pattern"], key):
        raise ProjectsError("key must match ^[a-z0-9][a-z0-9-]{0,11}$", "key")
    if not is_int(slot) or not LIMITS["slot_min"] <= slot <= LIMITS["slot_max"]:
        raise ProjectsError("slot must be an integer from 1 to 9", "slot")
    if not isinstance(name, str) or not name or utf8_len(name) > LIMITS["project_name_max"]:
        raise ProjectsError("name must be 1 to 24 UTF-8 bytes", "name")
    if not matches(NO_CONTROL, name):
        raise ProjectsError("name must not contain control characters", "name")


def validate_entry(raw: Any) -> Project:
    if not isinstance(raw, Mapping):
        raise ProjectsError("each project must be an object")
    key, root = raw.get("key"), raw.get("root")
    name = raw.get("name", key)
    _check_fields(key, name, raw.get("slot"))
    if root is not None and (not isinstance(root, str) or not os.path.isabs(root)):
        raise ProjectsError(f"project {key}: root must be an absolute path or null", "folder")
    mode = _check_mode(raw.get("spawn_permission_mode"))
    return Project(key, name, raw["slot"], root, mode, root is None or os.path.isdir(root))


def _check_unique(projects: tuple[Project, ...]) -> tuple[Project, ...]:
    for attr in ("key", "slot"):
        values = [getattr(p, attr) for p in projects]
        if len(values) != len(set(values)):
            raise ProjectsError(f"duplicate project {attr}", attr, "E_ALREADY")
    return projects


def parse_registry(doc: Any) -> tuple[Project, ...]:
    if not isinstance(doc, Mapping) or doc.get("version") != 1 or not isinstance(doc.get("projects"), list):
        raise ProjectsError('projects.json must be {"version": 1, "projects": [...]}')
    return _check_unique(tuple(validate_entry(p) for p in doc["projects"]))


def load_registry(path: Path) -> tuple[Project, ...]:
    if not path.exists():
        return ()
    try:
        doc = read_json(path)
    except ValueError as exc:
        raise ProjectsError("projects.json is not valid JSON") from exc
    return parse_registry(doc)


def dump_registry(projects: tuple[Project, ...]) -> dict[str, Any]:
    rows = [{"key": p.key, "name": p.name, "slot": p.slot, "root": p.root, "spawn_permission_mode": p.mode}
            for p in sorted(projects, key=lambda p: p.slot)]  # fmt: skip
    return {"version": 1, "projects": rows}


def save_registry(path: Path, projects: tuple[Project, ...]) -> None:
    write_json(path, dump_registry(projects))


def add_project(
    projects: tuple[Project, ...], *, key: str, path: str, slot: int, name: str | None = None, mode: str | None = None
) -> tuple[Project, ...]:
    if not os.path.isdir(path):
        raise ProjectsError("path must be an existing directory", "folder")
    entry = {"key": key, "name": name or key, "slot": slot, "root": os.path.realpath(path),
             "spawn_permission_mode": mode}  # fmt: skip
    return _check_unique((*projects, validate_entry(entry)))


def remove_project(projects: tuple[Project, ...], key: str) -> tuple[Project, ...]:
    kept = tuple(p for p in projects if p.key != key)
    if len(kept) == len(projects):
        raise ProjectsError(f"no project with key {key}", "key", "E_UNKNOWN_PROJECT")
    return kept


def find(projects: tuple[Project, ...], key: str | None) -> Project | None:
    return next((p for p in projects if p.key == key), None)
