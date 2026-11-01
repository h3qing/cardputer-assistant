"""Finding finished recordings as untrusted input (recording contract v1 README, docs/11 §7.8). I/O.

folders(root): recordings/<YYYY-MM-DD>/<session>/ folders that are real directories (lstat, no symlinks)
inside the root, have a manifest.json (a regular file of at most 1 MiB, read with O_NOFOLLOW) and no
result.json. check_audio(): every segment a regular file of the declared size, the canonical 16 kHz / 16 bit
/ mono header, and the whole-file sha256 of the manifest. foreign(): another program writes here now, i.e.
its status.json or transcript files changed in the last 5 minutes (older ones are leftovers the claim
holder may overwrite, never result.json); foreign_results(): result.json files another program placed
since cpa-stt took the claim (docs/22 §4.10).
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from cpa_stt import APP
from cpa_stt.writer import has_result

MANIFEST_MAX = 1 << 20
STATUS_MAX = 64 * 1024
DATE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}\Z")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
SKIP = "skip"
BLOCK = 1 << 20
THEIR_FILES = ("transcript.json", "transcript.md", "summary.md")
MAX_SAMPLES = 4 * 3600 * 16000  # a session holds at most 4 hours
LIVE_S = 300  # another program's file this recent means it is writing here now (the claim's 5 minutes)
RESULT_MAX = 64 * 1024


@dataclass(frozen=True, slots=True)
class Unreadable:
    """Why a JSON file could not be used; never its content."""

    reason: str


@dataclass(frozen=True, slots=True)
class Folder:
    path: Path
    rel: str
    doc: Any


def _is_dir(path: Path) -> bool:
    try:
        return stat.S_ISDIR(os.lstat(path).st_mode)
    except OSError:
        return False


def _children(path: Path, pattern: re.Pattern[str]) -> list[Path]:
    try:
        names = sorted(os.listdir(path))
    except OSError:
        return []
    return [path / n for n in names if pattern.match(n) and _is_dir(path / n)]


def read_json(path: Path, max_bytes: int) -> Any:
    """Parsed JSON of a regular file, Unreadable(reason), or None when there is no such file."""
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(st.st_mode):
        return Unreadable("(type)")
    if st.st_size > max_bytes:
        return Unreadable("(size)")
    try:
        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as fh:
            return json.loads(fh.read(max_bytes + 1))
    except (OSError, ValueError, RecursionError):
        return Unreadable("(root)")


def session_dirs(root: Path) -> tuple[tuple[str, Path], ...]:
    """(date/name, path) of every real session directory inside the root."""
    base = Path(os.path.realpath(root))
    found = ((f"{day.name}/{path.name}", path) for day in _children(base, DATE) for path in _children(day, NAME))
    return tuple((rel, path) for rel, path in found if os.path.realpath(path).startswith(str(base) + os.sep))


def folders(root: Path) -> tuple[Folder, ...]:
    """Sessions with a manifest.json and no result.json (symlinked manifests are ignored)."""
    out = []
    for rel, path in session_dirs(root):
        if has_result(path) or not os.path.lexists(path / "manifest.json"):
            continue
        doc = read_json(path / "manifest.json", MANIFEST_MAX)
        if doc != Unreadable("(type)"):
            out.append(Folder(path, rel, doc))
    return tuple(out)


def wav_header(data_len: int) -> bytes:
    fmt = struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
    return b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVEfmt " + fmt + b"data" + struct.pack("<I", data_len)


def _content(path: Path, size: int, sha256: str) -> str | None:
    digest = hashlib.sha256()
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as fh:
        header = fh.read(44)
        if header != wav_header(size - 44):
            return "E_AUDIO"
        digest.update(header)
        while block := fh.read(BLOCK):
            digest.update(block)
    return None if digest.hexdigest() == sha256 else "E_INVALID"


def check_audio(folder: Path, doc: dict) -> str | None:
    """None when every segment checks out; SKIP for a symlink or special file; else E_INVALID / E_AUDIO."""
    if sum(seg["samples"] for seg in doc["segments"]) > MAX_SAMPLES:
        return "E_INVALID"
    stats = []
    for seg in doc["segments"]:
        try:
            st = os.lstat(folder / seg["file"])
        except FileNotFoundError:
            return "E_INVALID"
        if not stat.S_ISREG(st.st_mode):
            return SKIP
        stats.append((seg, st.st_size))
    for seg, size in stats:
        problem = "E_INVALID" if size != seg["bytes"] else _content(folder / seg["file"], size, seg["sha256"])
        if problem:
            return problem
    return None


def regular_segments(folder: Path, doc: dict) -> bool:
    """Every segment of a valid manifest is a regular file (no symlink): only then is anything written."""
    try:
        return all(stat.S_ISREG(os.lstat(folder / seg["file"]).st_mode) for seg in doc["segments"])
    except OSError:
        return False


def ours(folder: Path) -> bool:
    status = read_json(folder / "status.json", STATUS_MAX)
    return isinstance(status, dict) and status.get("by") == APP


def _live(path: Path, now: float) -> bool:
    try:
        return now - os.lstat(path).st_mtime < LIVE_S
    except FileNotFoundError:
        return False


def foreign(folder: Path, now: float) -> bool:
    """Another transcriber writes here now: unless status.json is ours, any of its files changed in the last
    5 minutes (a time in the future counts as now)."""
    if ours(folder):
        return False
    return any(_live(folder / name, now) for name in ("status.json", *THEIR_FILES))


def _theirs(path: Path, since: float) -> bool:
    try:
        if os.lstat(path).st_mtime < since:
            return False
    except FileNotFoundError:
        return False
    doc = read_json(path, RESULT_MAX)
    pipeline = doc.get("pipeline") if isinstance(doc, dict) else None
    return not (isinstance(pipeline, dict) and pipeline.get("app") == APP)


def foreign_results(root: Path, since: float, since_day: str) -> tuple[str, ...]:
    """date/name of folders (from since_day on) whose result.json another program placed since `since`."""
    base = Path(os.path.realpath(root))
    sessions = (p for day in _children(base, DATE) if day.name >= since_day for p in _children(day, NAME))
    return tuple(f"{p.parent.name}/{p.name}" for p in sessions if _theirs(p / "result.json", since))
