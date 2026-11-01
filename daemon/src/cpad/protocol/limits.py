"""Protocol v0 limits, read from protocol/limits.json (the single machine-readable table, 05 §6.7).

cpad/protocol/limits.json is a symlink to that file. The build backend resolves it, so the sdist and the
wheel carry the file itself and an installed cpad needs no checkout (gh #57)."""

from __future__ import annotations

import json
import os
from collections.abc import Mapping
from importlib import resources
from importlib.resources.abc import Traversable
from pathlib import Path
from types import MappingProxyType
from typing import Any


def limits_source() -> Traversable:
    """The packaged table; CPA_PROTOCOL_DIR (a directory holding limits.json) overrides it."""
    override = os.environ.get("CPA_PROTOCOL_DIR")
    return Path(override) / "limits.json" if override else resources.files("cpad.protocol") / "limits.json"


def load_limits(path: Traversable | None = None) -> Mapping[str, Any]:
    source = path or limits_source()
    return MappingProxyType(json.loads(source.read_text(encoding="utf-8")))


LIMITS: Mapping[str, Any] = load_limits()
