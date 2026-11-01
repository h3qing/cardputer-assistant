"""Atomic writes into a recording folder (recording contract v1, docs/22 §4.7).

Each file goes to a dot-prefixed temp file (0600, O_EXCL | O_NOFOLLOW), is fsynced and renamed over the
target (a symlink at the target name is replaced, never followed), then the folder is fsynced. Nothing is
written once result.json exists; result.json itself is link()ed into place, so one that appeared meanwhile
always wins and is never overwritten.
"""

from __future__ import annotations

import contextlib
import os
from pathlib import Path

RESULT = "result.json"
FILE_MODE = 0o600


def has_result(folder: Path) -> bool:
    return os.path.lexists(folder / RESULT)


def fsync_dir(folder: Path) -> None:
    fd = os.open(folder, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _temp(folder: Path, name: str, data: bytes) -> Path:
    tmp = folder / f".{name}.{os.getpid()}.tmp"
    with contextlib.suppress(FileNotFoundError):
        tmp.unlink()
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, FILE_MODE)
    try:
        view = memoryview(data)
        while view:
            view = view[os.write(fd, view) :]
        os.fsync(fd)
    finally:
        os.close(fd)
    return tmp


def write_file(folder: Path, name: str, data: bytes) -> bool:
    """Write `name` atomically; False (and nothing written) when result.json is already there."""
    if has_result(folder):
        return False
    os.replace(_temp(folder, name, data), folder / name)
    fsync_dir(folder)
    return True


def place_result(folder: Path, data: bytes) -> bool:
    """Link result.json into place; False when one already exists (it is left as it is)."""
    tmp = _temp(folder, RESULT, data)
    try:
        os.link(tmp, folder / RESULT, follow_symlinks=False)
        placed = True
    except FileExistsError:
        placed = False
    finally:
        tmp.unlink()
    fsync_dir(folder)
    return placed
