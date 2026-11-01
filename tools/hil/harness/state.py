"""--state FILE (09 §9.3): the notes migrate_seed planted in the internal store, read back by
migrate_check (after the test firmware moved them to the SD card) and migrate_drained.

Pure apart from load_state / save_state. The seed is recorded by note text: the device only shows
an item id once it sends the item, and seeded items are never heard by the host, so migrate_check
learns the ids from the notes and stores them here.
"""

from __future__ import annotations

import json
import os
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

STATE_VERSION = 1


class StateError(ValueError):
    """The --state file is missing or is not one migrate_seed wrote."""


@dataclass(frozen=True, slots=True)
class Seed:
    dev: str  # the device that holds the seed
    boot: str  # hello.boot when it was seeded: every seeded id starts with it
    texts: tuple[str, ...]
    ids: tuple[str, ...] = ()  # filled in by migrate_check
    checked: bool = False  # migrate_check passed


def to_json(seed: Seed) -> dict[str, Any]:
    return {
        "version": STATE_VERSION,
        "dev": seed.dev,
        "boot": seed.boot,
        "texts": list(seed.texts),
        "ids": list(seed.ids),
        "checked": seed.checked,
    }


def _strings(value: Any, *, unique: bool) -> tuple[str, ...] | None:
    if not isinstance(value, list) or not all(isinstance(v, str) and v for v in value):
        return None
    if unique and len(set(value)) != len(value):
        return None
    return tuple(value)


def from_json(doc: Any) -> Seed:
    if not isinstance(doc, Mapping) or doc.get("version") != STATE_VERSION:
        raise StateError(f"not a version {STATE_VERSION} migration state")
    dev, boot = doc.get("dev"), doc.get("boot")
    if not (isinstance(dev, str) and dev and isinstance(boot, str) and boot):
        raise StateError("state needs the device and boot ids")
    texts = _strings(doc.get("texts"), unique=True)
    ids = _strings(doc.get("ids", []), unique=False)
    if not texts or ids is None or not isinstance(doc.get("checked", False), bool):
        raise StateError("state texts / ids / checked are malformed")
    return Seed(dev, boot, texts, ids, doc.get("checked", False))


def save_state(path: Path, seed: Seed) -> None:
    """Write the whole file to a temporary next to it, then replace: a crash leaves the old one."""
    path = Path(path)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(to_json(seed), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def load_state(path: Path) -> Seed:
    try:
        text = Path(path).read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise StateError(f"{path} does not exist: run migrate_seed first") from exc
    try:
        doc = json.loads(text)
    except ValueError as exc:
        raise StateError(f"{path} is not JSON") from exc
    return from_json(doc)
