"""Every reader gives the same effective project (docs/23 §1.3, §7.2): cpa rec, MCP list_recordings /
get_recording / list_todos / list_captures and the export, over one archive and one events.jsonl."""

from __future__ import annotations

import json

import pytest

from cpad.archive import events_view, export, todos
from cpad.archive.fs import Root
from cpad.cli.main import main as cpa
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.mcp import backend
from cpad.recordings import records as r
from tests.support import archive_tree as at

AT = "2026-10-21T22:00:00.000+00:00"


def _archived(rec: str, hhmm: str, mode: str, project: str | None) -> dict:
    return r.archived(dev="020000000001", rec=rec, dir=at.rec_dir("2026-10-21", hhmm, rec), mode=mode,
                      project=project, segments=1, dur_ms=60000, started_ms=at.MEETING_WALL, uncertain=False,
                      gaps=[], consent=None, at=AT)  # fmt: skip


@pytest.fixture
def home(short_tmp, tmp_path, monkeypatch):
    paths = Paths.at(short_tmp / "home")
    paths.home.mkdir(mode=0o700)
    root = Root.at(at.build(tmp_path / "notes"))
    write_json(paths.config, {"notes_dir": str(root.path)})
    projects = [{"key": "cpa", "name": "Cardputer", "slot": 1, "root": str(tmp_path)},
                {"key": "blog", "name": "Blog", "slot": 2, "root": str(tmp_path)}]  # fmt: skip
    write_json(paths.projects, {"version": 1, "projects": projects})
    meeting, memo = _archived(at.MEETING, "1402", "meeting", "cpa"), _archived(at.MEMO, "1405", "memo", None)
    done = r.result(rec=at.MEMO, state="done", level="summary", title="报价", memo_text="周五前发报价",
                    memo_intent="todo", actions_n=0, error_code=None, session=memo, at=AT)  # fmt: skip
    hand = {"ev": "queue.added", "at": AT, "qid": "q1", "project": "cpa", "text": "周五前发报价", "intent": "todo",
            "sid": None, "origin": "inbox", "from_item": at.MEMO, "uncertain": False}  # fmt: skip
    EventLog(paths.events).append((meeting, memo, done, r.edited_project(rec=at.MEETING, project="blog", at=AT), hand))
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    return paths, root


def _args(**kw):
    return {"from": None, "to": None, "mode": None, "project": None, "state": None, "kind": None, "status": "all",
            "limit": 20, "cursor": None, **kw}  # fmt: skip


WANT = {at.MEETING: "blog", at.MEMO: "cpa"}


def test_cpa_rec_ls_uses_the_effective_project(home, capsys):
    assert cpa(["rec", "ls", "--json"]) == 0
    rows = {row["sid"]: row["project"] for row in json.loads(capsys.readouterr().out)}
    assert rows == WANT


def test_mcp_recordings_and_their_filter_use_the_effective_project(home):
    reader = backend.ArchiveReader(*home)
    rows = {k.row["id"]: k.row["project"] for k in reader.recordings(_args(), None)}
    assert {sid: rows[sid] for sid in WANT} == WANT
    assert rows[at.BLOCKED] == "cpa", "a recording the log does not know keeps its manifest project"
    assert [k.row["id"] for k in reader.recordings(_args(project="blog"), None)] == [at.MEETING]
    assert reader.recording(at.MEETING.lower(), 20_000)["meta"]["project"] == "blog"
    assert reader.recording(at.MEMO.lower(), 20_000)["meta"]["project"] == "cpa"


def test_mcp_todos_and_captures_use_the_effective_project(home):
    reader = backend.ArchiveReader(*home)
    from_meeting = [k.row for k in reader.todos(_args(), None) if k.row["source"].get("id") == at.MEETING]
    assert from_meeting and {t["project"] for t in from_meeting} == {"blog"}
    memo = next(k.row for k in reader.captures(_args(kind="memo"), None))
    assert (memo["id"], memo["project"]) == (at.MEMO, "cpa")
    assert [k.row["id"] for k in reader.captures(_args(kind="memo", project="cpa"), None)] == [at.MEMO]
    with (home[1].path / "2026-10-21.md").open("a", encoding="utf-8") as note:
        note.write("- [ ] 15:10 #home 买花 ^cpa-3qzk1m7-0000d-0002\n")
    by_block = {
        k.row["block"]: k.row["project"] for k in reader.todos(_args(), None) if k.row["source"]["kind"] == "note"
    }
    assert by_block[f"cpa-{at.MEMO.lower()}"] == "cpa", "the memo's checkbox line in the daily note, filed after it"
    assert by_block["cpa-3qzk1m7-0000d-0002"] == "home", "a typed note keeps its own tag"
    assert f"cpa-{at.MEMO.lower()}" in {k.row["block"] for k in reader.todos(_args(project="cpa"), None)}


def test_the_archive_functions_keep_their_old_answer_without_a_mapping(home):
    paths, root = home
    page = todos.collect(root, status="all")
    assert {t.project for t in page.items if t.source_id == at.MEETING} == {"cpa"}
    events = EventLog(paths.events).load()
    assert [c.project for c in events_view.captures(events, kind="memo")] == [None]
    book = view.load(paths)
    assert [c.project for c in events_view.captures(events, kind="memo", projects=book.projects())] == ["cpa"]
    assert {t.project for t in todos.collect(root, status="all", projects=book.by_sid()).items
            if t.source_id == at.MEETING} == {"blog"}  # fmt: skip


def test_the_export_uses_the_effective_project(home, tmp_path):
    paths, root = home
    export.export_dir(paths, root, tmp_path / "out", export.ExportOptions(audio=False))
    rows = [json.loads(line) for line in (tmp_path / "out" / "data" / "filing.jsonl").read_text().splitlines()]
    assert {row["id"]: row["project"] for row in rows} == WANT
    assert {row["id"]: row["filing"] for row in rows} == {at.MEETING: "filed", at.MEMO: "handed"}
    captures = [json.loads(line) for line in (tmp_path / "out" / "data" / "captures.jsonl").read_text().splitlines()]
    assert [(c["id"], c["project"]) for c in captures if c["kind"] == "memo"] == [(at.MEMO, "cpa")]


def test_a_broken_projects_json_still_answers(home):
    paths, _ = home
    paths.projects.write_text("{")
    assert view.load(paths).projects()[at.MEETING] == "blog"
