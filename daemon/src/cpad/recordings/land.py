"""Staging and landing of pulled segments (docs/11 §6.2.7, §6.3). I/O shell around verify.py.

- staging: $CPA_HOME/rec-staging/<dev>/<fid>.part (directories 0700, file 0600);
- land(): fsync the .part, rename it to <notes_dir>/recordings/<YYYY-MM-DD>/<HHMM>-<sid lower>/seg-NNN.wav,
  fsync the directory. Across volumes (EXDEV) the .part is copied to .seg-NNN.wav.tmp in the target
  directory, fsynced and renamed, then the .part is removed. Directories 0700, files 0600.
"""

from __future__ import annotations

import errno
import hashlib
import os
import shutil
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from datetime import datetime, tzinfo
from pathlib import Path

from cpad.config.files import DIR_MODE, FILE_MODE
from cpad.protocol.file_messages import FileItem
from cpad.recordings import verify

READ_BLOCK = 1 << 20


class PartWriter:
    def __init__(self, fd: int) -> None:
        self._fd = fd

    def append(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            view = view[os.write(self._fd, view) :]

    def sync(self) -> None:
        os.fsync(self._fd)


def private_dirs(path: Path, root: Path) -> None:
    """Create `root` if missing and every directory from it down to `path` with mode 0700 (tightening
    looser ones below `root`; an existing `root` keeps its mode)."""
    root.mkdir(mode=DIR_MODE, parents=True, exist_ok=True)
    below = [d for d in (path, *path.parents) if root in d.parents]
    for directory in reversed(below):
        directory.mkdir(mode=DIR_MODE, exist_ok=True)
        if directory.stat().st_mode & 0o777 != DIR_MODE:
            directory.chmod(DIR_MODE)


class Staging:
    def __init__(self, root: Path) -> None:
        self.root = root

    def path(self, dev: str, fid: str) -> Path:
        return self.root / dev / f"{fid}.part"

    @staticmethod
    def size(path: Path) -> int:
        try:
            return path.stat().st_size
        except FileNotFoundError:
            return 0

    @contextmanager
    def open(self, path: Path, keep: int) -> Iterator[PartWriter]:
        """Open the .part for appending after truncating it to `keep` bytes."""
        private_dirs(path.parent, self.root.parent)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT, FILE_MODE)
        try:
            os.fchmod(fd, FILE_MODE)
            os.ftruncate(fd, keep)
            os.lseek(fd, keep, os.SEEK_SET)
            yield PartWriter(fd)
        finally:
            os.close(fd)


def session_dir(rec: str, wall_ms: int, tz: tzinfo | None) -> str:
    """recordings/<YYYY-MM-DD>/<HHMM>-<sid lower>, relative to notes_dir, from the session start."""
    when = datetime.fromtimestamp(wall_ms / 1000, tz)
    return f"recordings/{when:%Y-%m-%d}/{when:%H%M}-{rec.lower()}"


def seg_name(index: int) -> str:
    return f"seg-{index:03d}.wav"


def _digest(path: Path, skip: int = 0) -> tuple[bytes, str]:
    """(first 44 bytes, sha256 hex of the bytes after `skip`)."""
    sha = hashlib.sha256()
    with path.open("rb") as fh:
        head = fh.read(verify.HEADER)
        if not skip:
            sha.update(head)
        for block in iter(lambda: fh.read(READ_BLOCK), b""):
            sha.update(block)
    return head, sha.hexdigest()


def verify_file(path: Path, item: FileItem) -> verify.Verdict:
    head, data_sha = _digest(path, skip=verify.HEADER)
    return verify.check(item, path.stat().st_size, head, data_sha)


def file_sha256(path: Path) -> str:
    return _digest(path)[1]


def repair_header(path: Path) -> None:
    fd = os.open(path, os.O_WRONLY)
    try:
        os.pwrite(fd, verify.wav_header(os.fstat(fd).st_size - verify.HEADER), 0)
        os.fsync(fd)
    finally:
        os.close(fd)


def _fsync_path(path: Path, flags: int = os.O_RDONLY) -> None:
    fd = os.open(path, flags)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _copy_in(part: Path, target: Path, rename: Callable[[Path, Path], None]) -> None:
    tmp = target.parent / f".{target.name}.tmp"
    shutil.copyfile(part, tmp)
    os.chmod(tmp, FILE_MODE)
    _fsync_path(tmp)
    rename(tmp, target)
    part.unlink()


def land(part: Path, target: Path, *, root: Path, rename: Callable[[Path, Path], None] = os.replace) -> None:
    """Move a verified .part to its final name below `root` (notes_dir) atomically; fsync the directory."""
    _fsync_path(part)
    private_dirs(target.parent, root)
    try:
        rename(part, target)
    except OSError as exc:
        if exc.errno != errno.EXDEV:
            raise
        _copy_in(part, target, rename)
    os.chmod(target, FILE_MODE)
    _fsync_path(target.parent)
