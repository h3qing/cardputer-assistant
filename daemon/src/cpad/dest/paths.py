"""Path rules for `file.*` destinations (docs/14 §4.3, §11 item 6).

Appending "a line you said" to an arbitrary file is code execution when the file is ~/.zshrc, so:
only .md / .txt, no hidden or `..` segments, nothing under the TCC folders (Desktop, Documents,
Downloads, Library: iCloud Drive and the default $CPA_HOME live there). check_spec() is the lexical
part, run when destinations.json is parsed; check_place() adds the deny list for absolute roots too, run by
`cpa dest add` before it saves. target_path() runs at delivery time on the real file
system: realpath containment in root, no symlink as the last segment, the parent must already exist
(cpad never creates folders for you). append_line() opens with O_NOFOLLOW and is idempotent by marker.
"""

from __future__ import annotations

import errno
import os
import re
import stat
from collections.abc import Iterable
from pathlib import Path

from cpad.dest.model import FileOpts

DENIED_UNDER: tuple[str, ...] = ("Desktop", "Documents", "Downloads", "Library")  # relative to $HOME
SUFFIXES: tuple[str, ...] = (".md", ".txt")
FILE_MODE = 0o600


class PathRefused(ValueError):
    """A file destination path breaks a rule; .reason is one of
    outside_root symlink denied suffix hidden no_parent."""

    def __init__(self, reason: str, detail: str = ""):
        super().__init__(f"file path refused ({reason}){': ' + detail if detail else ''}")
        self.reason = reason


def _segments(text: str) -> list[str]:
    return text.split("/")


def _check_path(path: str) -> None:
    if not isinstance(path, str) or not path or path.startswith("/"):
        raise PathRefused("outside_root", "path must be relative to root")
    parts = _segments(path)
    if any(p in ("", "..") for p in parts):
        raise PathRefused("outside_root", "path must not contain '..' or empty segments")
    if any(p.startswith(".") for p in parts):
        raise PathRefused("hidden", "no segment may start with '.'")
    if not parts[-1].lower().endswith(SUFFIXES):
        raise PathRefused("suffix", "only .md and .txt files")


def _home_relative(root: str) -> list[str] | None:
    """Segments below $HOME for a "~" root; None for an absolute root."""
    if root == "~":
        return []
    return [p for p in _segments(root[2:]) if p] if root.startswith("~/") else None


def _check_root(root: str) -> list[str] | None:
    if not isinstance(root, str) or not (root == "~" or root.startswith(("~/", "/"))):
        raise PathRefused("outside_root", "root must be absolute or start with ~/")
    parts = [p for p in _segments(root.lstrip("~")) if p]
    if ".." in parts:
        raise PathRefused("outside_root", "root must not contain '..'")
    if any(p.startswith(".") for p in parts):
        raise PathRefused("hidden", "no root segment may start with '.'")
    return _home_relative(root)


def _denied_name(first: str) -> bool:
    return first.casefold() in {d.casefold() for d in DENIED_UNDER}


def check_spec(opts: FileOpts) -> None:
    """Lexical rules (parse time); raises PathRefused."""
    under_home = _check_root(opts.root)
    _check_path(opts.path)
    if under_home is not None:
        first = (under_home + _segments(opts.path))[0]
        if _denied_name(first):
            raise PathRefused("denied", f"~/{first} is not allowed")


def _within(path: str, root: str) -> bool:
    return path == root or path.startswith(root.rstrip("/") + "/")


def _root_dir(opts: FileOpts, home: Path) -> Path:
    if opts.root == "~":
        return home
    return home / opts.root[2:] if opts.root.startswith("~/") else Path(opts.root)


def _check_denied(real: str, home: Path, denied: Iterable[Path]) -> None:
    blocked = [os.path.realpath(home / d) for d in DENIED_UNDER] + [os.path.realpath(p) for p in denied]
    if any(_within(real.casefold(), b.casefold()) for b in blocked):
        raise PathRefused("denied", "inside a protected folder")


def check_place(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> None:
    """check_spec() plus the deny list for any root, "~" or absolute (cpa dest add, before saving); writes nothing.

    A shell turns `--root ~/Documents` into /Users/<you>/Documents, which the lexical "~" rule never sees,
    so the root and the file are resolved against home and refused under DENIED_UNDER or `denied` ($CPA_HOME).
    """
    check_spec(opts)
    denied = tuple(denied)
    root = os.path.realpath(_root_dir(opts, home))
    _check_denied(root, home, denied)
    _check_denied(os.path.realpath(os.path.join(root, opts.path)), home, denied)


def target_path(opts: FileOpts, home: Path, denied: Iterable[Path] = ()) -> Path:
    """The real file to append to (delivery time); raises PathRefused. `denied` adds folders such as $CPA_HOME."""
    check_spec(opts)
    denied = tuple(denied)
    real_root = os.path.realpath(_root_dir(opts, home))
    _check_denied(real_root, home, denied)
    lexical = Path(real_root, opts.path)
    real_parent = os.path.realpath(lexical.parent)
    if not os.path.isdir(real_parent):
        raise PathRefused("no_parent", "the folder does not exist")
    target = Path(real_parent, lexical.name)
    if not _within(real_parent, real_root):
        raise PathRefused("outside_root", "a folder on the way is a link out of root")
    if target.is_symlink():
        raise PathRefused("symlink", "the file is a symbolic link")
    _check_denied(str(target), home, denied)
    return target


def _open(path: Path) -> int:
    try:
        fd = os.open(path, os.O_RDWR | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, FILE_MODE)
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            raise PathRefused("symlink", "the file is a symbolic link") from exc
        raise
    if not stat.S_ISREG(os.fstat(fd).st_mode):
        os.close(fd)
        raise PathRefused("denied", "not a regular file")
    return fd


def _read_all(fd: int) -> bytes:
    return os.pread(fd, os.fstat(fd).st_size, 0)


def append_line(path: Path, line: str, marker: str) -> bool:
    """Append one line (O_APPEND|O_CREAT|O_NOFOLLOW, 0600, fsync). False when a line already ends with marker."""
    fd = _open(path)
    try:
        existing = _read_all(fd)
        if re.search(re.escape(marker.encode()) + rb"[ \t\r]*$", existing, re.MULTILINE):
            return False
        prefix = b"\n" if existing and not existing.endswith(b"\n") else b""
        os.write(fd, prefix + line.encode("utf-8") + b"\n")
        os.fsync(fd)
        return True
    finally:
        os.close(fd)
