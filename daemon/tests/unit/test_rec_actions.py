"""Recordings.on_device_request (docs/13 §8.2, §8.4, §12.2): rec.list pages and rec.act star / tag / hide.

rec.act only appends events (rec.edited / rec.hidden); no file under notes_dir is written, moved or deleted,
and manifest.json is never rewritten (recording contract v1).
"""

from __future__ import annotations

import hashlib
import json
import os
import time
from datetime import UTC
from pathlib import Path
from types import SimpleNamespace

import pytest

from cpad.config.settings import Config
from cpad.events.log import EventLog
from cpad.notes.writer import NotesWriter
from cpad.projects.registry import Project
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as rm
from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import history
from cpad.recordings import records as r
from cpad.recordings.service import Recordings

DEV = "020000000001"
AT = "2026-10-21T06:45:10.000+00:00"
T0 = 1792440000000
SID, MEMO = "3QZK1M7-00009-0001", "3QZK1M7-00009-0002"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
WHISPERWOOF = ("result.json", "summary.md", "transcript.md", "transcript.json", "status.json")


def archived(rec: str, started_ms: int, mode: str = "meeting") -> dict:
    folder = DIR if rec == SID else f"recordings/2026-10-21/1405-{rec.lower()}"
    return r.archived(dev=DEV, rec=rec, dir=folder, mode=mode, project="cpa", segments=1, dur_ms=2520000,
                      started_ms=started_ms, uncertain=False, gaps=[], consent=None, at=AT)  # fmt: skip


def done(session: dict, actions_n: int = 2) -> dict:
    return r.result(rec=session["rec"], state="done", level="summary", title="SECRET 客户同步", memo_text=None,
                    memo_intent=None, actions_n=actions_n, error_code=None, session=session, at=AT)  # fmt: skip


def seed(home: Path) -> EventLog:
    home.mkdir(mode=0o700, parents=True, exist_ok=True)
    log = EventLog(home / "events.jsonl")
    meeting = archived(SID, T0)
    log.append((meeting, done(meeting), archived(MEMO, T0 + 60000, "memo")))
    return log


def make(tmp_path: Path, appended: list | None = None) -> Recordings:
    log = seed(tmp_path / "home")
    notes = tmp_path / "notes"
    folder = notes / DIR
    folder.mkdir(parents=True)
    for name in ("seg-000.wav", "manifest.json", *WHISPERWOOF):
        (folder / name).write_text(f"{name} written by its owner\n- [ ] one\n- [x] two\n  * [ ] three\n+ [ ] four\n")
    sink = appended if appended is not None else []
    return Recordings(events=log, notes=NotesWriter(notes, UTC), staging=tmp_path / "home" / "rec-staging",
                      config=lambda: Config(notes_dir=str(notes)), history=log.load(), tz=UTC,
                      projects=lambda: (Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b")),
                      on_appended=lambda recs: sink.extend(recs))  # fmt: skip


def act(op: str, rec: str = SID, project: str | None = None, msg_id: str = "d3") -> rm.RecAct:
    return rm.RecAct(id=msg_id, rec=rec, op=op, project=project)


def listing(**kw) -> rm.RecList:
    return rm.RecList(**{"id": "d2", "filter": "all", "max": 6, **kw})


def edits(tmp_path: Path) -> list[dict]:
    lines = (tmp_path / "home" / "events.jsonl").read_text(encoding="utf-8").splitlines()
    return [e for e in map(json.loads, lines) if e["ev"] in ("rec.edited", "rec.hidden", "rec.unhidden")]


async def row(svc: Recordings, rec: str = SID) -> rm.RecRow | None:
    reply = await svc.on_device_request(listing())
    return next((item for item in reply.items if item.rec == rec), None)


def digests(folder: Path) -> dict[str, tuple[str, int]]:
    return {p.name: (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_mtime_ns) for p in folder.iterdir()}


# --- rec.act ----------------------------------------------------------------------------------


async def test_star_and_unstar_are_idempotent_and_acked(tmp_path):
    svc = make(tmp_path)
    for msg_id in ("d3", "d4"):
        assert await svc.on_device_request(act("star", msg_id=msg_id)) == m.Ack(re=msg_id)
    assert (await row(svc)).starred is True
    assert [e["starred"] for e in edits(tmp_path)] == [True]
    for _ in range(2):
        assert await svc.on_device_request(act("unstar")) == m.Ack(re="d3")
    assert (await row(svc)).starred is None
    assert [e["starred"] for e in edits(tmp_path)] == [True, False]


async def test_tag_sets_or_clears_the_project_idempotently(tmp_path):
    svc = make(tmp_path)
    assert await svc.on_device_request(act("tag", project="cpa")) == m.Ack(re="d3")
    assert edits(tmp_path) == []  # already cpa
    for project in ("blog", "blog", None, None):
        assert await svc.on_device_request(act("tag", project=project)) == m.Ack(re="d3")
    assert [e["project"] for e in edits(tmp_path)] == ["blog", None]
    assert (await row(svc)).project is None
    assert all(set(e) == {"ev", "at", "rec", "project"} for e in edits(tmp_path))


async def test_hide_leaves_the_listing_and_unhide_brings_it_back(tmp_path):
    svc = make(tmp_path)
    for _ in range(2):
        assert await svc.on_device_request(act("hide")) == m.Ack(re="d3")
    assert await row(svc) is None and [e["ev"] for e in edits(tmp_path)] == ["rec.hidden"]
    _, written = EventLog(tmp_path / "home" / "events.jsonl").append((r.unhidden(rec=SID, at=AT),))  # cpa rec unhide
    svc.absorb(written)
    assert (await row(svc)).rec == SID


async def test_actions_change_no_file_of_the_session(tmp_path):
    svc = make(tmp_path)
    folder = tmp_path / "notes" / DIR
    before = digests(folder)
    for msg in (act("star"), act("tag", project="blog"), act("tag", project=None), act("hide"), act("unstar")):
        assert await svc.on_device_request(msg) == m.Ack(re="d3")
    assert digests(folder) == before
    assert sorted(p.name for p in (tmp_path / "notes").rglob("*") if p.is_file()) == sorted(before)


async def test_the_hub_hears_about_every_appended_edit(tmp_path):
    appended: list = []
    svc = make(tmp_path, appended)
    await svc.on_device_request(act("hide", rec=MEMO))
    assert [e["ev"] for e in appended] == ["rec.hidden"] and appended[0]["rec"] == MEMO


async def test_an_unknown_session_is_e_unknown_item(tmp_path):
    svc = make(tmp_path)
    reply = await svc.on_device_request(act("hide", rec="3QZK1M7-00009-0ZZZ", msg_id="d8"))
    assert reply == m.ErrorMsg(re="d8", code="E_UNKNOWN_ITEM") and edits(tmp_path) == []


async def test_tag_with_a_project_cpad_does_not_know_is_e_bad_field(tmp_path):
    svc = make(tmp_path)
    reply = await svc.on_device_request(act("tag", project="gone"))
    assert reply == m.ErrorMsg(re="d3", code="E_BAD_FIELD", field="project") and edits(tmp_path) == []


async def test_a_failed_append_is_e_internal(tmp_path):
    svc = make(tmp_path)
    os.chmod(tmp_path / "home" / "events.jsonl", 0o400)
    assert await svc.on_device_request(act("star")) == m.ErrorMsg(re="d3", code="E_INTERNAL")


# --- rec.list ---------------------------------------------------------------------------------


async def test_rec_list_replies_with_one_page_newest_first(tmp_path):
    svc = make(tmp_path)
    reply = await svc.on_device_request(listing(id="d23"))
    assert isinstance(reply, rm.RecItems) and reply.re == "d23" and reply.next is None
    assert [item.rec for item in reply.items] == [MEMO, SID]
    first = await svc.on_device_request(listing(max=1))
    assert [item.rec for item in first.items] == [MEMO] and first.next is not None
    second = await svc.on_device_request(listing(max=1, cursor=first.next))
    assert [item.rec for item in second.items] == [SID] and second.next is None
    meetings = await svc.on_device_request(listing(filter="meeting"))
    assert [item.rec for item in meetings.items] == [SID]


async def test_a_session_on_the_device_without_a_clock_is_listed_at_the_start_it_will_land_with(tmp_path):
    """Recorded in the boot the link talks to now: resolved with the latest hello like landing will (docs/11 §6.5
    case 2), so it sits between the two stored sessions instead of first."""
    svc = make(tmp_path)
    boot, up_ms = SID[:13], 10**10
    await svc.on_ready(SimpleNamespace(), SimpleNamespace(boot=boot, up_ms=up_ms, fw=None, files=None, dev=DEV))
    start = T0 + 30_000
    at = At(up_ms - (int(time.time() * 1000) - start))
    early = FileItem(id=f"{boot}-0009", kind="rec", size=3244, rec=f"{boot}-0009", seg=0, last=True, mode="memo",
                     dur_ms=4000, at=at)  # fmt: skip
    svc.puller = SimpleNamespace(listing=(early,), current=None)
    reply = await svc.on_device_request(listing())
    assert [item.rec for item in reply.items] == [MEMO, early.rec, SID]
    assert abs(reply.items[1].wall_ms - start) < 5000 and reply.items[1].st == "on_device"


async def test_todo_n_counts_the_unchecked_boxes_of_summary_md(tmp_path):
    svc = make(tmp_path)
    assert (await row(svc)).todo_n == 3  # one, three and four; "- [x] two" is done
    summary = tmp_path / "notes" / DIR / "summary.md"
    summary.write_text("no boxes at all\n")
    assert (await row(svc)).todo_n == 0


@pytest.mark.parametrize("kind", ["missing", "symlink", "huge"])
async def test_an_unreadable_summary_md_falls_back_to_the_result(tmp_path, kind):
    svc = make(tmp_path)
    summary = tmp_path / "notes" / DIR / "summary.md"
    summary.unlink()
    if kind == "symlink":
        (tmp_path / "elsewhere.md").write_text("- [ ] a\n- [ ] b\n- [ ] c\n- [ ] d\n- [ ] e\n")
        summary.symlink_to(tmp_path / "elsewhere.md")
    elif kind == "huge":
        summary.write_text("- [ ] x\n" * 20000)
    assert (await row(svc)).todo_n == 2  # actions_n of the result event


async def test_a_cursor_cpad_did_not_make_is_e_bad_field(tmp_path):
    svc = make(tmp_path)
    reply = await svc.on_device_request(listing(cursor="t1792440000000-a1"))
    assert reply == m.ErrorMsg(re="d2", code="E_BAD_FIELD", field="cursor")


async def test_an_unexpected_error_is_e_internal_and_logged_without_text(tmp_path, monkeypatch, caplog):
    svc = make(tmp_path)

    def broken(*args, **kwargs):
        raise RuntimeError("boom")

    monkeypatch.setattr(history, "page", broken)
    assert await svc.on_device_request(listing()) == m.ErrorMsg(re="d2", code="E_INTERNAL")
    assert "SECRET" not in caplog.text


async def test_any_other_request_is_e_unknown_type(tmp_path):
    svc = make(tmp_path)
    reply = await svc.on_device_request(m.QueueDone(id="d9", qid="q1"))
    assert reply == m.ErrorMsg(re="d9", code="E_UNKNOWN_TYPE")
