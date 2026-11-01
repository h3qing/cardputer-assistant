"""Does a path sync online? (docs/15 §2.4) `cpa export` refuses such targets unless --allow-cloud.

A path counts as cloud when it lies
- under ~/Library/Mobile Documents (iCloud Drive) or ~/Library/CloudStorage (Dropbox, Google Drive, OneDrive…);
- under ~/Desktop or ~/Documents while iCloud "Desktop & Documents" keeps them (their twins exist in iCloud
  Drive; the folders themselves stay real directories, so a realpath does not reveal it);
- under a legacy sync folder at the top of home: ~/Dropbox…, ~/Google Drive…, ~/OneDrive…;
- or below a folder a macOS File Provider marked (com.apple.file-provider-domain-id, com.apple.icloud.desktop).
Best effort: a sync tool that leaves none of these marks is not detected. Paths are compared resolved.
"""

from __future__ import annotations

import ctypes
import functools
import os
import sys
from collections.abc import Callable, Iterator
from pathlib import Path

CLOUD_DIRS = ("Library/Mobile Documents", "Library/CloudStorage")
ICLOUD_DRIVE = "Library/Mobile Documents/com~apple~CloudDocs"
DESKTOP_AND_DOCUMENTS = ("Desktop", "Documents")
LEGACY_PREFIXES = ("Dropbox", "Google Drive", "OneDrive")
MARKS = frozenset({"com.apple.file-provider-domain-id", "com.apple.icloud.desktop"})
XATTR_NOFOLLOW = 0x0001
Marks = Callable[[Path], frozenset[str]]


def _real(path: Path) -> Path:
    return Path(os.path.realpath(path))


def _inside(path: Path, base: Path) -> bool:
    return path == base or base in path.parents


@functools.cache
def _listxattr() -> Callable[..., int]:
    fn = ctypes.CDLL(None, use_errno=True).listxattr
    fn.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int)
    fn.restype = ctypes.c_ssize_t
    return fn


def _darwin_names(raw: bytes) -> frozenset[str]:
    listxattr = _listxattr()
    size = listxattr(raw, None, 0, XATTR_NOFOLLOW)
    if size <= 0:
        return frozenset()
    buf = ctypes.create_string_buffer(size)
    size = listxattr(raw, buf, size, XATTR_NOFOLLOW)
    return frozenset(name.decode("utf-8", "replace") for name in buf.raw[: max(0, size)].split(b"\0") if name)


def xattr_names(path: Path) -> frozenset[str]:
    """Extended attribute names of `path` itself (a symlink is not followed); empty off macOS or when unreadable."""
    if sys.platform != "darwin":
        return frozenset()
    return _darwin_names(os.fsencode(path))


def _roots(home: Path) -> Iterator[Path]:
    yield from (_real(home / d) for d in CLOUD_DIRS)
    yield from (_real(home / d) for d in DESKTOP_AND_DOCUMENTS if (home / ICLOUD_DRIVE / d).is_dir())


def _legacy(path: Path, home: Path) -> bool:
    real_home = _real(home)
    return real_home in path.parents and path.relative_to(real_home).parts[0].startswith(LEGACY_PREFIXES)


def in_cloud(path: Path, home: Path, marks: Marks = xattr_names) -> bool:
    """Whether the resolved `path` lies in a folder that syncs online; `home` is the user's home directory."""
    if any(_inside(path, root) for root in _roots(home)) or _legacy(path, home):
        return True
    return any(marks(p) & MARKS for p in (path, *path.parents) if os.path.lexists(p))
