"""Which program transcribes recordings/, for `cpa status` (recording contract v1, docs/22 §4.10). Read-only.

The running transcriber (WhisperWoof or cpa-stt) claims the folder with recordings/.transcriber
{v, app, pid, since, beat_at} and renews beat_at every 60 s; a claim older than 5 minutes is stale. A beat
in the future counts as the file's mtime, and a time without a UTC offset makes the claim unreadable (it
ages by mtime). cpad only reads it (a regular file of at most 4 KiB, no symlink); it holds no transcription
code itself.
"""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Any

from cpad.recordings.results import Unreadable, read_small

CLAIM = ".transcriber"
CLAIM_MAX = 4096
FRESH_S = 300


def _beat(doc: Any, mtime: float) -> tuple[str | None, int | None, float]:
    if isinstance(doc, Unreadable) or not isinstance(doc, dict):
        return None, None, mtime
    app, pid = doc.get("app"), doc.get("pid")
    try:
        when = datetime.fromisoformat(str(doc.get("beat_at")))
    except ValueError:
        return None, None, mtime
    ok = isinstance(app, str) and 0 < len(app) <= 16 and isinstance(pid, int) and not isinstance(pid, bool)
    return (app, pid, min(when.timestamp(), mtime)) if ok and when.tzinfo is not None else (None, None, mtime)


def describe(root: Path | None, now: float) -> dict[str, Any]:
    got = read_small(root / CLAIM, CLAIM_MAX) if root is not None else None
    if got is None:
        return {"app": None, "pid": None, "beat_age_s": None, "fresh": False}
    app, pid, beat = _beat(got[1], got[0])
    age = max(0, int(now - beat))
    return {"app": app, "pid": pid, "beat_age_s": age, "fresh": age <= FRESH_S}


def line(info: dict[str, Any]) -> str:
    age = info["beat_age_s"]
    if age is None:
        return "none running"
    who = f"{info['app']} pid {info['pid']}" if info["app"] else "unreadable claim"
    return f"{who}, beat {age} s ago" if info["fresh"] else f"{who}, not running (last beat {age // 60} min ago)"
