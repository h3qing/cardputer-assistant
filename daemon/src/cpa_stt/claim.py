"""recordings/.transcriber: one transcriber at a time (recording contract v1, docs/22 §4.10). I/O.

The claim is {v:1, app, pid, since, beat_at}. The running transcriber renews beat_at every 60 s, also while
idle or blocked. A claim by anyone else (another program, or another cpa-stt process that is still alive)
whose last beat is less than 5 minutes old means: write nothing. An unreadable claim (or a time without a
UTC offset) ages by its mtime, and a beat in the future counts as the mtime. A missing claim is created
with link(), so of two takers only one wins; a stale one is replaced and read back. Release removes only
our own claim.
"""

from __future__ import annotations

import contextlib
import os
import re
from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from cpa_stt import APP
from cpa_stt.contract_out import encode
from cpa_stt.scan import read_json
from cpa_stt.writer import fsync_dir

NAME = ".transcriber"
FRESH_S = 300
BEAT_S = 60
CLAIM_MAX = 4096
APP_NAME = re.compile(r"^[a-z0-9-]{1,16}\Z")


@dataclass(frozen=True, slots=True)
class Claim:
    app: str
    pid: int
    since: str
    beat_at: str


@dataclass(frozen=True, slots=True)
class Seen:
    claim: Claim | None
    beat: float


def _ts(text: object) -> float | None:
    """Seconds of an RFC 3339 time with a UTC offset; None without one (it would be read as local time)."""
    try:
        when = datetime.fromisoformat(str(text))
    except ValueError:
        return None
    return when.timestamp() if when.tzinfo is not None else None


def _parse(doc: object) -> Claim | None:
    if not isinstance(doc, dict) or doc.get("v") != 1 or not isinstance(doc.get("pid"), int):
        return None
    app, since, beat = doc.get("app"), doc.get("since"), doc.get("beat_at")
    if not isinstance(app, str) or not APP_NAME.match(app) or _ts(since) is None or _ts(beat) is None:
        return None
    return Claim(app, doc["pid"], str(since), str(beat))


def read(root: Path) -> Seen | None:
    path = root / NAME
    try:
        mtime = os.lstat(path).st_mtime
    except FileNotFoundError:
        return None
    parsed = _parse(read_json(path, CLAIM_MAX))
    return Seen(parsed, min(_ts(parsed.beat_at) or mtime, mtime)) if parsed else Seen(None, mtime)


def decide(seen: Seen | None, *, me: int, now: float, alive: Callable[[int], bool]) -> str:
    """'mine', 'take' (no claim, a stale one, or a dead cpa-stt's) or 'busy' (someone else is running)."""
    if seen is None:
        return "take"
    ours = seen.claim is not None and seen.claim.app == APP
    if ours and seen.claim.pid == me:
        return "mine"
    if now - seen.beat > FRESH_S or (ours and not alive(seen.claim.pid)):
        return "take"
    return "busy"


def _temp(root: Path, pid: int, since: str, now: str) -> Path:
    tmp = root / f".{NAME}.{pid}.tmp"
    with contextlib.suppress(FileNotFoundError):
        tmp.unlink()
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "wb") as fh:
        fh.write(encode({"v": 1, "app": APP, "pid": pid, "since": since, "beat_at": now}))
        fh.flush()
        os.fsync(fh.fileno())
    return tmp


def write(root: Path, pid: int, *, since: str, now: str) -> None:
    """Renew our own claim (heartbeat)."""
    os.replace(_temp(root, pid, since, now), root / NAME)
    fsync_dir(root)


def holds(root: Path, pid: int) -> bool:
    seen = read(root)
    return seen is not None and seen.claim is not None and (seen.claim.app, seen.claim.pid) == (APP, pid)


def take(root: Path, pid: int, *, since: str, now: str, stale: bool) -> bool:
    """Claim recordings/: link() a missing claim into place (only one taker wins), or replace a stale one;
    then read it back. True when the claim is ours."""
    tmp = _temp(root, pid, since, now)
    try:
        if stale:
            os.replace(tmp, root / NAME)
        else:
            os.link(tmp, root / NAME, follow_symlinks=False)
    except FileExistsError:
        return False
    finally:
        with contextlib.suppress(FileNotFoundError):
            tmp.unlink()
    fsync_dir(root)
    return holds(root, pid)


def release(root: Path, pid: int) -> bool:
    if not holds(root, pid):
        return False
    with contextlib.suppress(FileNotFoundError):
        (root / NAME).unlink()
    return True


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def release_dead(root: Path, alive: Callable[[int], bool]) -> bool:
    """Remove a cpa-stt claim whose process is gone (what `cpa-stt off` cleans up after a crash)."""
    seen = read(root)
    if seen is None or seen.claim is None or seen.claim.app != APP or alive(seen.claim.pid):
        return False
    with contextlib.suppress(FileNotFoundError):
        (root / NAME).unlink()
    return True
