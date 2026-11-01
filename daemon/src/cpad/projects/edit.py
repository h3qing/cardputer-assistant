"""Projects as named groups (docs/25 §8, 2026-10-26): what `cpa project add|rename|move|folder|rm` do.

A project is a name the device shows on a number key 1-9; the voice you file under it stays in this app until you
hand it to an agent yourself. A code folder is optional: only a linked project gets hooks, cwd matching and spawns.
Every function returns a new tuple and never changes the one it was given; a refusal is a ProjectsError whose
code / field are the filing/v1 error (nothing is written).
"""

from __future__ import annotations

import os
import re
from dataclasses import replace

from cpad.config.host import valid_name
from cpad.projects import registry
from cpad.projects.registry import Project, ProjectsError
from cpad.protocol.limits import LIMITS

KEY_MAX = 12
_NOT_KEY = re.compile(r"[^a-z0-9]+")


def _slots() -> range:
    return range(LIMITS["slot_min"], LIMITS["slot_max"] + 1)


def _check_name(name: object) -> str:
    if not valid_name(name):
        raise ProjectsError("a name is 1-24 UTF-8 bytes without control or direction characters, not starting "
                            "with '-'", "name")  # fmt: skip
    return name


def _unique(base: str, taken: set[str]) -> str:
    if base not in taken:
        return base
    for n in range(2, 100):
        tail = f"-{n}"
        candidate = base[: KEY_MAX - len(tail)].rstrip("-") + tail
        if candidate not in taken:
            return candidate
    raise ProjectsError("no free key; give one with --key", "key", "E_ALREADY")


def derive_key(name: str, slot: int, taken: set[str]) -> str:
    """ASCII letters and digits of the name joined by '-', at most 12; a name without any (家务) gets p<slot>."""
    slug = _NOT_KEY.sub("-", name.lower()).strip("-")[:KEY_MAX].rstrip("-")
    return _unique(slug or f"p{slot}", taken)


def _free_slot(projects: tuple[Project, ...]) -> int:
    used = {p.slot for p in projects}
    free = [s for s in _slots() if s not in used]
    if not free:
        raise ProjectsError("all 9 slots are taken; remove a project first", "slot", "E_ALREADY")
    return free[0]


def _check_slot(projects: tuple[Project, ...], slot: object) -> int:
    if not isinstance(slot, int) or isinstance(slot, bool) or slot not in _slots():
        raise ProjectsError("slot must be an integer from 1 to 9", "slot")
    if any(p.slot == slot for p in projects):
        raise ProjectsError(f"slot {slot} is taken; move that project or pick another slot", "slot", "E_ALREADY")
    return slot


def _check_key(projects: tuple[Project, ...], key: str, used: frozenset[str]) -> None:
    if registry.find(projects, key) is not None:
        raise ProjectsError(f"key {key} is taken", "key", "E_ALREADY")
    if key in used:
        raise ProjectsError(f"recordings already carry key {key}; pass --reuse to give them to this project",
                            "key", "E_ALREADY")  # fmt: skip


def _folder(path: str | None) -> str | None:
    if path is None:
        return None
    if not os.path.isdir(path):
        raise ProjectsError("the folder must be an existing directory", "folder")
    return os.path.realpath(path)


def new_project(
    projects: tuple[Project, ...],
    *,
    name: str,
    key: str | None = None,
    slot: int | None = None,
    folder: str | None = None,
    mode: str | None = None,
    used: frozenset[str] = frozenset(),
    reuse: bool = False,
) -> tuple[Project, ...]:
    """Add one: the key defaults to one derived from the name, the slot to the first free one.

    `used` are the keys history still carries (recordings, queue items, usual destinations of removed projects):
    a derived key avoids them, and an explicit one is refused unless `reuse` hands that history to this project.
    """
    name = _check_name(name)
    slot = _free_slot(projects) if slot is None else _check_slot(projects, slot)
    taken = {p.key for p in projects} | set(used)
    key = derive_key(name, slot, taken) if key is None else key
    entry = {"key": key, "name": name, "slot": slot, "root": _folder(folder), "spawn_permission_mode": mode}
    added = registry.validate_entry(entry)
    _check_key(projects, added.key, frozenset() if reuse else used)
    return (*projects, added)


def _existing(projects: tuple[Project, ...], key: str) -> Project:
    found = registry.find(projects, key)
    if found is None:
        raise ProjectsError(f"no project with key {key}", "key", "E_UNKNOWN_PROJECT")
    return found


def _replaced(projects: tuple[Project, ...], changed: dict[str, Project]) -> tuple[Project, ...]:
    return tuple(changed.get(p.key, p) for p in projects)


def rename(projects: tuple[Project, ...], key: str, name: str) -> tuple[Project, ...]:
    found = _existing(projects, key)
    return _replaced(projects, {key: replace(found, name=_check_name(name))})


def move(projects: tuple[Project, ...], key: str, slot: int) -> tuple[Project, ...]:
    """Put `key` on `slot`; the project already there (if any) takes key's old slot."""
    found = _existing(projects, key)
    _check_slot((), slot)
    other = next((p for p in projects if p.slot == slot and p.key != key), None)
    changed = {key: replace(found, slot=slot)}
    if other is not None:
        changed = {**changed, other.key: replace(other, slot=found.slot)}
    return _replaced(projects, changed)


def link(projects: tuple[Project, ...], key: str, folder: str | None) -> tuple[Project, ...]:
    """Link a code folder (stored as realpath), or unlink with None."""
    found = _existing(projects, key)
    root = _folder(folder)
    return _replaced(projects, {key: replace(found, root=root, ok=True)})


def remove(projects: tuple[Project, ...], key: str) -> tuple[Project, ...]:
    """Forget the project; its recordings and queue items keep their key (filing still shows it, docs/23 §3.3)."""
    _existing(projects, key)
    return tuple(p for p in projects if p.key != key)
