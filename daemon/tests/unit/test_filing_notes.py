"""Filing and the daily note (docs/23 §2.4): only cpad writes the '↪ Filed' line, from a self-contained rec.filed it
appends in housekeeping; start-up replays it; a session filed before its result gets the new project at once."""

from __future__ import annotations

import json
from datetime import UTC
from pathlib import Path

import pytest

from cpad.config.settings import Config
from cpad.events.log import EventLog
from cpad.notes.writer import NotesWriter
from cpad.projects.registry import Project
from cpad.protocol import rec_messages as rm
from cpad.recordings import records as r
from cpad.recordings.service import Recordings
from tests.support import archive_tree as at

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

DEV = "020000000001"
AT = "2026-10-22T03:23:00.000+00:00"
MEMO = "3QZK1M7-0000A-0001"
DIR = f"recordings/2026-10-22/0323-{MEMO.lower()}"
STARTED = 1792639380000  # 2026-10-22 03:23 UTC
PROJECTS = (Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b"))
LINE = f"- 03:23 #cpa ↪ 归档：报价 ^cpa-{MEMO.lower()}-f1-cpa"


def archived() -> dict:
    return r.archived(dev=DEV, rec=MEMO, dir=DIR, mode="memo", project=None, segments=1, dur_ms=24000,
                      started_ms=STARTED, uncertain=False, gaps=[], consent=None, at=AT)  # fmt: skip


def result_doc() -> dict:
    memo = {"text": "周五前发报价", "intent": "note"}
    return {**at.with_session("result_memo_todo", MEMO), "title": "报价", "memo": memo}


def make(tmp: Path, *history: dict) -> Recordings:
    home, notes = tmp / "home", tmp / "notes"
    home.mkdir(mode=0o700, parents=True, exist_ok=True)
    (notes / DIR).mkdir(parents=True, exist_ok=True)
    log = EventLog(home / "events.jsonl")
    if history:
        log.append(history)
    writer = NotesWriter(notes, UTC)
    loaded = log.load()
    writer.backfill(loaded)
    return Recordings(events=log, notes=writer, staging=home / "rec-staging",
                      config=lambda: Config(notes_dir=str(notes)), projects=lambda: PROJECTS, history=loaded,
                      tz=UTC)  # fmt: skip


def from_cli(tmp: Path, svc: Recordings, *recs: dict) -> None:
    """Another process appends (cpa filing file); housekeeping's poll hands the lines to Recordings."""
    _, written = EventLog(tmp / "home" / "events.jsonl").append(recs)
    svc.absorb(written)


def events(tmp: Path, ev: str) -> list[dict]:
    lines = (tmp / "home" / "events.jsonl").read_text(encoding="utf-8").splitlines()
    return [e for e in map(json.loads, lines) if e["ev"] == ev]


def note(tmp: Path) -> list[str]:
    path = tmp / "notes" / "2026-10-22.md"
    return path.read_text(encoding="utf-8").splitlines() if path.exists() else []


async def test_filed_after_the_result_appends_one_rec_filed_and_one_line(tmp_path):
    (tmp_path / "notes" / DIR).mkdir(parents=True)
    (tmp_path / "notes" / DIR / "result.json").write_text(json.dumps(result_doc()), encoding="utf-8")
    svc = make(tmp_path, archived())
    await svc.housekeep()
    assert [line.split(" ^")[0] for line in note(tmp_path)] == [f"- 03:23 语音：周五前发报价 [音频]({DIR}/seg-000.wav)"]
    from_cli(tmp_path, svc, r.edited_project(rec=MEMO, project="cpa", at=AT))
    await svc.housekeep()
    await svc.housekeep()
    (filed,) = events(tmp_path, "rec.filed")
    assert (filed["project"], filed["title"], filed["dir"]) == ("cpa", "报价", DIR)
    assert note(tmp_path)[1:] == [LINE]
    assert "周五前发报价" not in json.dumps(filed), "rec.filed never carries memo or transcript text"


async def test_start_up_replays_rec_filed_and_writes_the_one_an_offline_cli_left_out(tmp_path):
    done = r.result(rec=MEMO, state="done", level="summary", title="报价", memo_text="周五前发报价",
                    memo_intent="note", actions_n=0, error_code=None, session=archived(), at=AT)  # fmt: skip
    svc = make(tmp_path, archived(), done, r.edited_project(rec=MEMO, project="cpa", at=AT))  # cpad was not running
    await svc.housekeep()
    assert note(tmp_path)[1:] == [LINE] and len(events(tmp_path, "rec.filed")) == 1
    again = make(tmp_path)  # a restart: backfill replays every line once
    await again.housekeep()
    assert note(tmp_path)[1:] == [LINE] and len(events(tmp_path, "rec.filed")) == 1


async def test_filing_back_to_an_earlier_project_ends_the_note_on_it(tmp_path):
    done = r.result(rec=MEMO, state="done", level="summary", title="报价", memo_text="周五前发报价",
                    memo_intent="note", actions_n=0, error_code=None, session=archived(), at=AT)  # fmt: skip
    svc = make(tmp_path, archived(), done)
    for key in ("cpa", "blog", "cpa"):  # e.g. blog by mistake, then ⌘Z
        from_cli(tmp_path, svc, r.edited_project(rec=MEMO, project=key, at=AT, by="cli"))
        await svc.housekeep()
    assert [line.split(" ↪")[0][-4:] for line in note(tmp_path)[1:]] == ["#cpa", "blog", "#cpa"]
    assert note(tmp_path)[-1] == f"- 03:23 #cpa ↪ 归档：报价 ^cpa-{MEMO.lower()}-f3-cpa"
    again = make(tmp_path)  # a restart replays the three lines, once each
    await again.housekeep()
    assert len(note(tmp_path)) == 4 and len(events(tmp_path, "rec.filed")) == 3


async def test_filed_before_the_result_needs_no_extra_line(tmp_path):
    svc = make(tmp_path, archived())
    from_cli(tmp_path, svc, r.edited_project(rec=MEMO, project="blog", at=AT))
    (tmp_path / "notes" / DIR / "result.json").write_text(json.dumps(result_doc()), encoding="utf-8")
    await svc.housekeep()
    (result,) = events(tmp_path, "rec.result")
    assert result["project"] == "blog" and events(tmp_path, "rec.filed") == []
    assert note(tmp_path)[0].startswith("- 03:23 #blog 语音：周五前发报价")


async def test_a_device_tag_after_the_result_is_filed_too_and_rows_show_the_effective_project(tmp_path):
    done = r.result(rec=MEMO, state="done", level="summary", title=None, memo_text="x", memo_intent="note",
                    actions_n=0, error_code=None, session=archived(), at=AT)  # fmt: skip
    svc = make(tmp_path, archived(), done)
    assert await svc.on_device_request(rm.RecAct(id="d3", rec=MEMO, op="tag", project="blog"))
    await svc.housekeep()
    assert note(tmp_path)[-1] == f"- 03:23 #blog ↪ 归档：语音 ^cpa-{MEMO.lower()}-f1-blog"
    hand = {"ev": "queue.added", "at": AT, "qid": "q1", "project": "cpa", "text": "x", "intent": "todo", "sid": None,
            "origin": "inbox", "from_item": MEMO, "uncertain": False}  # fmt: skip
    from_cli(tmp_path, svc, hand)
    assert [row["project"] for row in svc.rows()] == ["cpa"]
    await svc.housekeep()
    assert [e["project"] for e in events(tmp_path, "rec.filed")] == ["blog", "cpa"]
