"""Recording event records for events.jsonl (docs/11 §6.8). Pure constructors; records are plain dicts.

rec.seg.stored  a segment landed and verified (appended, fsynced, before file.del is sent)
rec.seg.deleted the device acknowledged file.del
rec.archived    manifest.json written for a session
rec.result      result.json seen, once per (session, state)
rec.stale       no result.json transcribe_wait_h hours after archiving
rec.audio.swept the session's WAVs were removed by the 30-day retention sweep (text stays)
rec.edited      v0.4.1: the recordings app or the CLI starred / unstarred a session or set its project;
                docs/23 §2.2: or marked it done (looked at, no project needed) or not
rec.hidden      v0.4.1: hidden from the recordings app and `cpa rec ls` (docs/13 §8.4); no file is touched
rec.unhidden    v0.4.1: `cpa rec unhide`
                docs/23 §2.2: `cpa filing` adds by:"cli" to the three; readers ignore it
rec.filed       docs/23 §2.4: cpad filed a session after its daily-note line was written; it repeats the session
                context and the result's title, so it alone renders the '↪ Filed' line (route.rec_entry); n counts
                the session's rec.filed records, so each one has a block id of its own

rec.result and rec.stale repeat the session context of rec.archived (dir, mode, project, started_ms,
uncertain, dur_ms, and dest when segment 0 carried one) so that one event is enough to render its
daily-note line (route.rec_entry). dest (v0.4.1, docs/13 §9.3) is written only when the device sent one,
so v0.3 records keep their shape.
env (docs/23 §4.2, stamp.py): segment 0's rec.seg.stored carries the stamp, null when it could not be computed;
rec.archived carries it only when manifest.json does.
No record ever carries transcript text; title / memo_text are what the daily note shows (05 §7.8).
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from cpad.protocol.file_messages import FileItem
from cpad.recordings.wall import Wall

CONTEXT = ("dir", "mode", "project", "started_ms", "uncertain", "dur_ms")


def _dest(dest: str | None) -> dict[str, str]:
    return {"dest": dest} if dest else {}


def seg_stored(*, dev: str, item: FileItem, file: str, file_sha256: str, wall: Wall, repaired: bool,
               fw: str | None, at: str, env: Mapping[str, Any] | None = None) -> dict[str, Any]:  # fmt: skip
    return {
        "ev": "rec.seg.stored", "at": at, "dev": dev, "id": item.id, "rec": item.rec, "seg": item.seg,
        "last": item.last, "mode": item.mode, "size": item.size, "sha256": item.sha256, "file": file,
        "file_sha256": file_sha256, "project": item.project, "slot": item.slot, "stars": list(item.stars),
        "lead_ms": item.lead_ms, "consent": item.consent, "rcv": bool(item.rcv), "repaired": repaired,
        "at_dev": None if item.at is None else {"up_ms": item.at.up_ms, "wall_ms": item.at.wall_ms},
        "wall_ms": wall.wall_ms, "uncertain": wall.uncertain, "fw": fw, **_dest(item.dest),
        **({"env": None if env is None else dict(env)} if item.seg == 0 else {}),
    }  # fmt: skip


def seg_deleted(dev: str, seg_id: str, at: str) -> dict[str, Any]:
    return {"ev": "rec.seg.deleted", "at": at, "dev": dev, "id": seg_id}


def archived(
    *,
    dev: str,
    rec: str,
    dir: str,
    mode: str,
    project: str | None,
    segments: int,
    dur_ms: int,
    started_ms: int,
    uncertain: bool,
    gaps: list[int],
    consent: str | None,
    at: str,
    dest: str | None = None,
    env: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    return {"ev": "rec.archived", "at": at, "dev": dev, "rec": rec, "dir": dir, "mode": mode, "project": project,
            "segments": segments, "dur_ms": dur_ms, "started_ms": started_ms, "uncertain": uncertain,
            "gaps": list(gaps), "consent": consent, **_dest(dest),
            **({"env": dict(env)} if env is not None else {})}  # fmt: skip


def context(archived_record: Mapping[str, Any]) -> dict[str, Any]:
    return {**{k: archived_record.get(k) for k in CONTEXT}, **_dest(archived_record.get("dest"))}


def result(*, rec: str, state: str, level: str | None, title: str | None, memo_text: str | None,
           memo_intent: str | None, actions_n: int, error_code: str | None, session: Mapping[str, Any],
           at: str) -> dict[str, Any]:  # fmt: skip
    return {"ev": "rec.result", "at": at, "rec": rec, "state": state, "level": level, "title": title,
            "memo_text": memo_text, "memo_intent": memo_intent, "actions_n": actions_n, "error_code": error_code,
            **context(session)}  # fmt: skip


def stale(*, rec: str, session: Mapping[str, Any], at: str) -> dict[str, Any]:
    return {"ev": "rec.stale", "at": at, "rec": rec, **context(session)}


def audio_swept(rec: str, files: int, at: str) -> dict[str, Any]:
    return {"ev": "rec.audio.swept", "at": at, "rec": rec, "files": files}


def edited_star(*, rec: str, starred: bool, at: str) -> dict[str, Any]:
    return {"ev": "rec.edited", "at": at, "rec": rec, "starred": starred}


def _by(by: str | None) -> dict[str, str]:
    return {"by": by} if by else {}


def edited_project(*, rec: str, project: str | None, at: str, by: str | None = None) -> dict[str, Any]:
    """project None: the session belongs to no project (docs/13 §8.2, key 0)."""
    return {"ev": "rec.edited", "at": at, "rec": rec, "project": project, **_by(by)}


def edited_done(*, rec: str, done: bool, at: str, by: str | None = None) -> dict[str, Any]:
    """docs/23 §2.2: looked at, needs no project (done) or back to new; it changes no project and no star."""
    return {"ev": "rec.edited", "at": at, "rec": rec, "done": done, **_by(by)}


def hidden(*, rec: str, at: str, by: str | None = None) -> dict[str, Any]:
    return {"ev": "rec.hidden", "at": at, "rec": rec, **_by(by)}


def unhidden(*, rec: str, at: str, by: str | None = None) -> dict[str, Any]:
    return {"ev": "rec.unhidden", "at": at, "rec": rec, **_by(by)}


def filed(*, rec: str, project: str, session: Mapping[str, Any], title: str | None, n: int,
          at: str) -> dict[str, Any]:  # fmt: skip
    """n: this session's n-th rec.filed (1, 2, ...), part of its line's block id."""
    return {"ev": "rec.filed", "at": at, "rec": rec, "project": project, "dir": session.get("dir"),
            "mode": session.get("mode"), "started_ms": session.get("started_ms"),
            "uncertain": bool(session.get("uncertain")), "title": title, "n": n}  # fmt: skip
