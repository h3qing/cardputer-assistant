"""A $CPA_HOME and archive for `cpa filing` (docs/23 §3): five recordings in every filing state, over
tests/support/archive_tree. MEETING is filed to cpa, MEMO is new with its memo text (an escape in its title, a
U+2028 in its text), OLD is new with only a transcript, FAILED asked for another Mac's project (acme) and failed,
BLOCKED is hidden. Project cpa's name carries a bidi control and a root that does not exist."""

from __future__ import annotations

from pathlib import Path

from cpad.archive.fs import Root
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from cpad.recordings import records as r
from tests.support import archive_tree as at

AT = "2026-10-21T22:00:00.000+00:00"
DEV = "020000000001"
SECRET_ROOT = "/Users/someone/src/SECRET-ROOT"


def _archived(rec: str, day: str, hhmm: str, mode: str, project: str | None, wall: int) -> dict:
    return r.archived(dev=DEV, rec=rec, dir=at.rec_dir(day, hhmm, rec), mode=mode, project=project, segments=1,
                      dur_ms=24000, started_ms=wall, uncertain=False, gaps=[], consent=None, at=AT)  # fmt: skip


def _result(session: dict, state: str = "done", level: str | None = "summary", **over) -> dict:
    fields = {"title": None, "memo_text": None, "memo_intent": None, "actions_n": 0, "error_code": None, **over}
    return r.result(rec=session["rec"], state=state, level=level, session=session, at=AT, **fields)


def _stored(rec: str, day: str, hhmm: str) -> dict:
    return {"ev": "rec.seg.stored", "at": AT, "dev": DEV, "id": rec, "rec": rec, "seg": 0, "mode": "memo",
            "file": f"{at.rec_dir(day, hhmm, rec)}/seg-000.wav", "fw": "cpa-fw/0.4.0"}  # fmt: skip


def build(home: Path, notes: Path) -> Paths:
    """home: a new $CPA_HOME (short, for the socket path); notes: a new archive directory."""
    paths = Paths.at(home)
    paths.home.mkdir(mode=0o700)
    root = Root.at(at.build(notes))
    write_json(paths.config, {"notes_dir": str(root.path)})
    write_json(paths.projects, {"version": 1, "projects": [
        {"key": "cpa", "name": "Card\u202eputer", "slot": 1, "root": SECRET_ROOT},
        {"key": "blog", "name": "Blog", "slot": 2, "root": str(notes)}]})  # fmt: skip
    meeting = _archived(at.MEETING, "2026-10-21", "1402", "meeting", "cpa", at.MEETING_WALL)
    memo = _archived(at.MEMO, "2026-10-21", "1405", "memo", None, at.MEETING_WALL + 180_000)
    old = _archived(at.OLD, "2026-10-20", "0915", "meeting", None, at.MEETING_WALL - 103_620_000)
    failed = _archived(at.FAILED, "2026-10-20", "1100", "meeting", "acme", at.MEETING_WALL - 97_320_000)
    blocked = _archived(at.BLOCKED, "2026-10-21", "1630", "meeting", None, at.MEETING_WALL + 8_880_000)
    EventLog(paths.events).append((
        _stored(at.MEMO, "2026-10-21", "1405"), meeting, memo, old, failed, blocked,
        _result(meeting, title="客户同步：定价和交付"),
        _result(memo, title="报\x1b[31m价", memo_text="周五前给 Alice 发报价单。\u2028第二行", memo_intent="todo"),
        _result(old, level="transcript"), _result(failed, state="failed", level=None, error_code="E_STT"),
        r.hidden(rec=at.BLOCKED, at=AT)))  # fmt: skip
    return paths
