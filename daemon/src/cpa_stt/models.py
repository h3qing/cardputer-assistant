"""`cpa-stt setup`: link model files the example needs into $CPA_HOME/models (docs/22 §4.4). I/O.

It looks for the example's <MODEL:pattern> files first in a few known folders (the one docs/22 §4.4 names,
~/.cache/openwhispr/whisper-models, which Spotlight skips) and then with Spotlight (mdfind -name), shows the
largest regular file matching each pattern and links it only after you say yes (or --yes). A path given on
the command line is linked directly. Either way the file must be yours (or root's), neither it nor its
folder writable by others, not on an external volume (/Volumes), and start with the ggml magic. It never
copies and never downloads anything, and never replaces a real file already there.
"""

from __future__ import annotations

import contextlib
import fnmatch
import os
import stat
from collections.abc import Callable, Iterable
from pathlib import Path

from cpad.config.files import ensure_private_dir

MDFIND = "/usr/bin/mdfind"
GGML_MAGIC = b"lmgg"  # whisper.cpp ggml models (and the Silero VAD) start with 0x67676d6c, little-endian


class ModelError(ValueError):
    """A model cannot be linked (the message says why)."""


def known_dirs() -> tuple[Path, ...]:
    return (Path.home() / ".cache" / "openwhispr" / "whisper-models",)


def _regular(path: Path) -> int | None:
    """Size of a non-empty regular file (not a symlink), else None."""
    try:
        st = os.lstat(path)
    except OSError:
        return None
    return st.st_size if stat.S_ISREG(st.st_mode) and st.st_size > 0 else None


def _largest(paths: Iterable[Path], pattern: str) -> tuple[Path, ...]:
    found = [(p, _regular(p)) for p in paths if fnmatch.fnmatchcase(p.name, pattern)]
    return tuple(p for p, size in sorted((x for x in found if x[1]), key=lambda x: (-(x[1] or 0), str(x[0]))))


def in_dirs(pattern: str, dirs: Iterable[Path]) -> tuple[Path, ...]:
    """Regular files named like `pattern` directly in the known folders, largest first."""
    return _largest((p for d in dirs if d.is_dir() for p in d.iterdir()), pattern)


def spotlight(pattern: str, run: Callable[[list[str]], str], skip: Path) -> tuple[Path, ...]:
    """Regular files named like `pattern` that Spotlight knows, largest first (none inside `skip`)."""
    literal = pattern.split("*")[0].split("?")[0] or pattern
    lines = (Path(line) for line in run([MDFIND, "-name", literal]).splitlines() if line.startswith("/"))
    return _largest((p for p in lines if skip not in p.parents), pattern)


def present(models: Path, pattern: str) -> Path | None:
    names = sorted(p.name for p in models.glob("*") if fnmatch.fnmatchcase(p.name, pattern) and p.exists())
    return models / names[-1] if names else None


def matching(path: Path, patterns: Iterable[str]) -> str | None:
    return next((p for p in patterns if fnmatch.fnmatchcase(path.name, p)), None)


def _untrusted(target: Path) -> str | None:
    """Why `target` (a realpath) is not a model cpa-stt will hand to the engine; None when it is."""
    if str(target).startswith("/Volumes/"):
        return "is on an external volume (/Volumes)"
    if _regular(target) is None:
        return "is not a model file"
    for path in (target, target.parent):
        info = os.stat(path)
        if info.st_uid not in (os.getuid(), 0):
            return f"({path}) belongs to another user"
        if info.st_mode & 0o022:
            return f"({path}) can be changed by other users"
    with os.fdopen(os.open(target, os.O_RDONLY | os.O_NOFOLLOW), "rb") as fh:
        return None if fh.read(4) == GGML_MAGIC else "is not a ggml model (no ggml magic)"


def link(models: Path, source: Path) -> Path:
    """models/<name> -> realpath(source), a symlink; an existing symlink is replaced, a real file is not."""
    target = Path(os.path.realpath(source))
    why = _untrusted(target)
    if why:
        raise ModelError(f"{source} {why}")
    ensure_private_dir(models)
    dest = models / source.name
    if os.path.lexists(dest) and not dest.is_symlink():
        raise ModelError(f"{dest} is already there and is not a link; leaving it")
    tmp = models / f".{source.name}.{os.getpid()}.link"
    with contextlib.suppress(FileNotFoundError):
        tmp.unlink()
    tmp.symlink_to(target)
    os.replace(tmp, dest)
    return dest
