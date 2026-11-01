"""Private file helpers for $CPA_HOME (05 §7.1: directory 0700, files 0600)."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

DIR_MODE = 0o700
FILE_MODE = 0o600


def ensure_private_dir(path: Path) -> None:
    path.mkdir(mode=DIR_MODE, parents=True, exist_ok=True)
    if path.stat().st_mode & 0o777 != DIR_MODE:
        path.chmod(DIR_MODE)


def atomic_write(path: Path, data: bytes, mode: int = FILE_MODE) -> None:
    """Write to a temp file, fsync, rename over the target: readers never see a partial file."""
    tmp = path.with_name(f".{path.name}.tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode)
    try:
        os.write(fd, data)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def write_json(path: Path, doc: Any) -> None:
    atomic_write(path, (json.dumps(doc, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))
