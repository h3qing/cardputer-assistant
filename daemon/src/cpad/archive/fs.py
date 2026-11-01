"""Confined reads under one archive root (docs/15 §3.8). I/O shell.

- paths are only ever built from names that match NAME_RE (ids.py makes them), one component at a time;
- every directory level is opened with O_DIRECTORY | O_NOFOLLOW relative to its parent (openat), so a
  symlink at any level is refused without a check-then-use race; the file itself is opened with
  O_NOFOLLOW | O_NONBLOCK (a FIFO never blocks) and fstat must show a regular file within its size cap;
- dot names (temporary files, .obsidian) and names outside [A-Za-z0-9._-] are never opened or listed;
- finally the realpath must still lie inside the root.
Errors carry a code only (E_NOT_FOUND | E_TOO_LARGE | E_UNSAFE | E_IO), never a path or any content.
"""

from __future__ import annotations

import errno
import itertools
import os
import re
import stat
import sys
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO

NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\Z")
DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
FILE_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
LIST_MAX = 20_000
CHUNK = 1 << 16
UNSAFE_ERRNOS = frozenset({errno.ELOOP, errno.ENOTDIR})

Parts = tuple[str, ...]


class ArchiveError(Exception):
    """A confined read failed; `code` is one of E_NOT_FOUND, E_TOO_LARGE, E_UNSAFE, E_IO."""

    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True, slots=True)
class Root:
    path: Path

    @classmethod
    def at(cls, path: Path | str) -> Root:
        """The realpath, resolved once: the root itself may be a symlink, nothing below it may."""
        return cls(Path(os.path.realpath(Path(path).expanduser())))


def safe_name(name: object) -> bool:
    return isinstance(name, str) and NAME_RE.match(name) is not None


def _code(exc: OSError) -> str:
    if exc.errno == errno.ENOENT:
        return "E_NOT_FOUND"
    return "E_UNSAFE" if exc.errno in UNSAFE_ERRNOS else "E_IO"


def _check_names(parts: Parts) -> None:
    if not all(safe_name(p) for p in parts):
        raise ArchiveError("E_UNSAFE")


def _inside(root: Root, parts: Parts) -> None:
    real = os.path.realpath(root.path.joinpath(*parts))
    if os.path.commonpath([str(root.path), real]) != str(root.path):
        raise ArchiveError("E_UNSAFE")


def _open(root: Root, parts: Parts, last_flags: int) -> int:
    """Open the root, then each component relative to its parent; intermediate descriptors are closed."""
    try:
        fd = os.open(root.path, DIR_FLAGS)
    except OSError as exc:
        raise ArchiveError(_code(exc)) from None
    for i, part in enumerate(parts):
        flags = last_flags if i == len(parts) - 1 else DIR_FLAGS
        try:
            child = os.open(part, flags, dir_fd=fd)
        except OSError as exc:
            raise ArchiveError(_code(exc)) from None
        finally:
            os.close(fd)
        fd = child
    return fd


def _open_file(root: Root, parts: Parts, max_bytes: int) -> int:
    if not parts:
        raise ArchiveError("E_UNSAFE")
    _check_names(parts)
    fd = _open(root, parts, FILE_FLAGS)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode):
            raise ArchiveError("E_UNSAFE")
        if st.st_size > max_bytes:
            raise ArchiveError("E_TOO_LARGE")
        _inside(root, parts)
    except BaseException:
        os.close(fd)
        raise
    return fd


def _read(fh: BinaryIO, size: int) -> bytes:
    try:
        return fh.read(size)
    except OSError:
        raise ArchiveError("E_IO") from None


def chunks(root: Root, parts: Parts, max_bytes: int) -> Iterator[bytes]:
    """The file's bytes in blocks; E_TOO_LARGE also when it grows past the cap while being read."""
    fd = _open_file(root, parts, max_bytes)
    with os.fdopen(fd, "rb") as fh:
        total = 0
        while block := _read(fh, CHUNK):
            total += len(block)
            if total > max_bytes:
                raise ArchiveError("E_TOO_LARGE")
            yield block


def read_bytes(root: Root, parts: Parts, max_bytes: int) -> bytes:
    return b"".join(chunks(root, parts, max_bytes))


def read_text(root: Root, parts: Parts, max_bytes: int) -> str:
    """UTF-8 with errors replaced (notes are edited by hand and by other programs)."""
    return read_bytes(root, parts, max_bytes).decode("utf-8", errors="replace")


def open_fd(root: Root, parts: Parts, max_bytes: int) -> int:
    """A confined read-only descriptor of a regular file, for a child process that reads it as /dev/fd/N (a
    player): nothing can swap the file between this check and that read. The caller closes it."""
    return _open_file(root, parts, max_bytes)


def file_size(root: Root, parts: Parts) -> int:
    fd = _open_file(root, parts, sys.maxsize)
    try:
        return os.fstat(fd).st_size
    finally:
        os.close(fd)


def _keep(entry: os.DirEntry, kind: str | None) -> bool:
    if not safe_name(entry.name) or entry.is_symlink():
        return False
    if kind == "dir":
        return entry.is_dir(follow_symlinks=False)
    return entry.is_file(follow_symlinks=False) if kind == "file" else True


def _take(entries: Iterable[os.DirEntry], kind: str | None, max_entries: int) -> tuple[str, ...]:
    names = tuple(itertools.islice((e.name for e in entries if _keep(e, kind)), max_entries + 1))
    if len(names) > max_entries:
        raise ArchiveError("E_TOO_LARGE")
    return tuple(sorted(names))


def list_names(root: Root, parts: Parts, max_entries: int = LIST_MAX, kind: str | None = None) -> tuple[str, ...]:
    """Sorted names in a directory (kind: "file", "dir" or None); a missing directory lists as empty."""
    _check_names(parts)
    try:
        fd = _open(root, parts, DIR_FLAGS)
    except ArchiveError as exc:
        if exc.code == "E_NOT_FOUND":
            return ()
        raise
    try:
        _inside(root, parts)
        with os.scandir(fd) as entries:
            return _take(entries, kind, max_entries)
    except OSError:
        raise ArchiveError("E_IO") from None
    finally:
        os.close(fd)
