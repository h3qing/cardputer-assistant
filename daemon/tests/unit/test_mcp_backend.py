"""The production Reader / Writer over cpad.archive and events.jsonl (docs/15 §3.4, §3.6), on a real temp archive."""

from __future__ import annotations

import json
import socket
import threading
from datetime import date

import jsonschema
import pytest

from cpad.archive.fs import ArchiveError, Root
from cpad.config.files import write_json
from cpad.config.settings import Paths
from cpad.events.log import EventLog
from cpad.mcp import backend, tools
from cpad.mcp.catalog import READ_TOOLS, WRITE_TOOLS
from cpad.mcp.tools import ToolError
from tests.support import archive_tree as at

SCHEMAS = {t.name: t.output_schema for t in READ_TOOLS + WRITE_TOOLS}
NOTE_ITEM = {"ev": "item.received", "at": "2026-10-21T07:00:00.000+00:00", "dev": "020000000001",
             "id": "3QZK1M7-0000D-0001", "kind": "note", "body": {"text": "Pricing idea", "project": "home"},
             "at_dev": {"up_ms": 1, "wall_ms": None}, "received_at": "2026-10-21T07:00:00.000+00:00"}  # fmt: skip


@pytest.fixture
def paths(short_tmp):
    home = short_tmp / "home"
    home.mkdir(mode=0o700)
    root = short_tmp / "proj"
    root.mkdir()
    write_json(home / "projects.json", {"version": 1, "projects": [
        {"key": "cpa", "name": "Cardputer 助手", "slot": 1, "root": str(root)}]})  # fmt: skip
    EventLog(home / "events.jsonl").append((NOTE_ITEM,))
    return Paths.at(home)


@pytest.fixture
def root(tmp_path):
    return Root.at(at.build(tmp_path / "notes"))


@pytest.fixture
def reader(paths, root):
    return backend.ArchiveReader(paths, root)


def _args(**kw):
    base = {"from": None, "to": None, "mode": None, "project": None, "state": None, "kind": None, "status": "open",
            "limit": 20, "cursor": None}  # fmt: skip
    return {**base, **kw}


def test_days(reader):
    rows = list(reader.days(_args(), None))
    assert [r.key for r in rows] == ["2026-10-21", "2026-10-20"]
    assert rows[0].row["recordings"] == 4 and rows[0].row["bytes"] > 0
    assert [r.key for r in reader.days(_args(), "2026-10-21")] == ["2026-10-20"]


def test_day(reader):
    page = reader.day(date(2026, 10, 20), 1, 20_000)
    assert page["date"] == "2026-10-20" and "给 Bob 回电话" in page["text"] and page["next_offset"] is None


def test_recordings_with_filters_and_after(reader):
    rows = list(reader.recordings(_args(), None))
    assert [r.row["id"] for r in rows][:2] == [at.PULLING, at.BLOCKED]
    meetings = [r.row["id"] for r in reader.recordings(_args(mode="meeting", state="done"), None)]
    assert meetings == [at.MEETING]
    after = [r.row["id"] for r in reader.recordings(_args(), rows[0].key)]
    assert after[0] == at.BLOCKED


def test_recordings_are_lazy(reader, monkeypatch):
    built = []
    real = backend.recordings.row

    def counting(root, ref):
        built.append(ref.sid)
        return real(root, ref)

    monkeypatch.setattr(backend.recordings, "row", counting)
    first = next(iter(reader.recordings(_args(), None)))
    assert first.row["id"] == at.PULLING and len(built) == 1


def test_hidden_recordings_are_invisible(paths, reader):
    EventLog(paths.events).append(({"ev": "rec.hidden", "at": "x", "rec": at.MEETING},))
    assert at.MEETING not in [r.row["id"] for r in reader.recordings(_args(), None)]
    with pytest.raises(ArchiveError):
        reader.recording(at.MEETING.lower(), 20_000)
    with pytest.raises(ArchiveError):
        reader.transcript(at.MEETING.lower(), 0, 20_000)


def test_hidden_memos_are_not_captures(paths, reader):
    memo = {"ev": "rec.result", "at": "x", "rec": at.MEMO, "state": "done", "mode": "memo",
            "memo_text": "SECRET-HIDDEN-MEMO", "started_ms": at.MEETING_WALL, "project": "cpa"}  # fmt: skip
    EventLog(paths.events).append((memo,))
    assert "SECRET-HIDDEN-MEMO" in [k.row["text"] for k in reader.captures(_args(), None)]
    EventLog(paths.events).append(({"ev": "rec.hidden", "at": "x", "rec": at.MEMO},))
    rows = [k.row for k in reader.captures(_args(), None)]
    assert [r["kind"] for r in rows] == ["note"] and "SECRET-HIDDEN-MEMO" not in str(rows)
    assert [k.row["kind"] for k in reader.captures(_args(kind="memo"), None)] == []


WAITING = "3QZK1M7-00045-0000"


def test_a_recording_waiting_for_the_transcriber_is_called_waiting(root, reader):
    at.write(root.path / at.rec_dir("2026-10-21", "1800", WAITING) / "manifest.json", at.manifest(WAITING))
    rows = {k.row["id"]: k.row["state"] for k in reader.recordings(_args(), None)}
    assert rows[WAITING] == "waiting" and "archived" not in rows.values()
    assert [k.row["id"] for k in reader.recordings(_args(state="waiting"), None)] == [WAITING]
    assert reader.recording(WAITING.lower(), 20_000)["state"] == "waiting"


def test_recording_detail(reader):
    doc = reader.recording(at.MEETING.lower(), 20_000)
    assert doc["id"] == at.MEETING and doc["state"] == "done"
    assert doc["meta"]["mode"] == "meeting" and doc["result"]["actions"][0]["text"] == "给 Alice 发报价单"
    assert "pricing" in doc["summary"]["text"] and doc["summary"]["truncated"] is False


def test_an_unreadable_summary_is_an_error_not_an_empty_text(paths, root, reader):
    folder = root.path / at.rec_dir("2026-10-21", "1402", at.MEETING)
    (folder / "summary.md").write_bytes(b"x" * (2 << 20))
    assert reader.recording(at.MEETING.lower(), 20_000)["summary"] == {"text": "", "truncated": False,
                                                                        "error": "E_TOO_LARGE"}  # fmt: skip
    body = tools.call(backend.context(paths, root), "get_recording", {"id": at.MEETING}).body
    assert body["summary"]["error"] == "E_TOO_LARGE"
    jsonschema.validate(body, SCHEMAS["get_recording"])
    assert "error" not in reader.recording(at.MEMO.lower(), 20_000)["summary"]


def test_unknown_recording(reader):
    with pytest.raises(ArchiveError) as err:
        reader.recording("3qzk1m7-99999-0000", 20_000)
    assert err.value.code == "E_NOT_FOUND"


def test_transcript(reader):
    page = reader.transcript(at.MEETING.lower(), 1, 20_000)
    assert page["total"] == 3 and page["sentences"][0]["i"] == 1 and page["next_offset"] is None


def test_todos_page_one_past_the_limit(reader):
    rows = list(reader.todos(_args(status="all", limit=1), None))
    assert len(rows) == 2
    assert rows[0].row["source"]["kind"] in ("note", "recording")
    open_rows = [r.row for r in reader.todos(_args(), None)]
    assert all(not r["done"] for r in open_rows)
    assert {k: v for k, v in open_rows[0]["source"].items() if k != "line"} == {"kind": "recording", "id": at.MEETING}


def test_captures_and_queue(paths, reader):
    caps = list(reader.captures(_args(), None))
    assert caps[0].row["text"] == "Pricing idea" and caps[0].row["kind"] == "note"
    assert list(reader.captures(_args(), caps[0].key)) == []
    w = backend.EventsWriter(paths, clock=lambda: 1792620120.0)
    assert w.queue_add("cpa", "修一下") == "q1"
    assert w.queue_add("cpa", "再修一下") == "q2"
    items = [k.row for k in reader.queue(None)]
    assert [i["qid"] for i in items] == ["q2", "q1"] and items[0]["origin"] == "mcp"


def test_writer_refuses_unknown_projects(paths):
    with pytest.raises(ToolError) as err:
        backend.EventsWriter(paths).queue_add("nope", "x")
    assert err.value.field == "project"
    events = [json.loads(line) for line in paths.events.read_text().splitlines()]
    assert all(e["ev"] != "queue.added" for e in events)


def test_writer_with_broken_projects_is_unavailable(paths):
    paths.projects.write_text("{")
    with pytest.raises(ToolError) as err:
        backend.EventsWriter(paths).queue_add("cpa", "x")
    assert err.value.code == "E_UNAVAILABLE"


def test_search(reader):
    found = reader.search("PRICING", ("notes", "summaries", "transcripts"), None, None, 20, None)
    kinds = {h["source"]["kind"] for h in found["hits"]}
    assert {"notes", "summaries", "transcripts"} <= kinds and found["scanned"] > 0 and found["next"] is None
    transcript_hit = next(h for h in found["hits"] if h["source"]["kind"] == "transcripts")
    assert transcript_hit["source"] == {"kind": "transcripts", "id": at.MEETING, "matches": 1}
    assert isinstance(transcript_hit["t_s"], float) and transcript_hit["i"] == 0
    note_hit = next(h for h in found["hits"] if h["source"]["kind"] == "notes")
    assert note_hit["offset"] == note_hit["line"] - 1


BUDGET = "3QZK1M7-00045-0000"


def _long_meeting(root):
    folder = root.path / at.rec_dir("2026-10-21", "1640", BUDGET)
    at.write(folder / "manifest.json", at.manifest(BUDGET))
    said = [{"t_ms": k * 10_000, "text": f"第 {k} 次说到预算。"} for k in range(60)]
    at.write(folder / "transcript.json", {**at.with_session("transcript_meeting", BUDGET), "sentences": said})
    summary = root.path / at.rec_dir("2026-10-21", "1402", at.MEETING) / "summary.md"
    summary.write_text(summary.read_text(encoding="utf-8") + "预算还没定。\n", encoding="utf-8")


def test_search_over_mcp_shows_every_meeting_and_where_to_read_on(paths, root):
    _long_meeting(root)
    ctx = backend.context(paths, root)
    body = tools.call(ctx, "search", {"query": "预算"}).body
    sources = [(h["source"]["kind"], h["source"]["id"], h["source"]["matches"]) for h in body["hits"]]
    assert sources == [("transcripts", BUDGET, 60)] * 3 + [("summaries", at.MEETING, 1)]
    third = body["hits"][2]
    page = tools.call(ctx, "read_transcript", {"id": BUDGET, "offset": third["i"]}).body
    assert page["sentences"][0]["text"] == "第 2 次说到预算。"
    jsonschema.validate(body, SCHEMAS["search"])


def test_search_pages_over_mcp(paths, root):
    _long_meeting(root)
    ctx = backend.context(paths, root)
    seen, cursor = [], None
    while True:
        body = tools.call(ctx, "search", {"query": "预算", "limit": 1} | ({"cursor": cursor} if cursor else {})).body
        seen += [(h["source"]["id"], h.get("i"), h.get("line")) for h in body["hits"]]
        cursor = body.get("next_cursor")
        if cursor is None:
            break
    assert seen == [(BUDGET, 0, None), (BUDGET, 1, None), (BUDGET, 2, None), (at.MEETING, None, 12)]


def test_search_refuses_a_forged_position(paths, root):
    ctx = backend.context(paths, root)
    forged = tools.encode_cursor("hits", "2026-10-21/0/../../etc/1#0")
    assert tools.call(ctx, "search", {"query": "x", "cursor": forged}).error.field == "cursor"


@pytest.mark.parametrize(
    ("name", "raw"),
    [("list_days", {}), ("read_daily_note", {"date": "2026-10-21"}), ("list_recordings", {}),
     ("get_recording", {"id": at.MEETING}), ("get_recording", {"id": at.MEMO}), ("get_recording", {"id": at.BLOCKED}),
     ("read_transcript", {"id": at.MEETING}), ("read_transcript", {"id": at.OLD}), ("list_todos", {"status": "all"}),
     ("list_captures", {}), ("list_queue", {}), ("get_status", {}), ("search", {"query": "pricing"})],
)  # fmt: skip
def test_every_tool_on_a_real_archive_matches_its_output_schema(paths, root, name, raw):
    ctx = backend.context(paths, root)
    out = tools.call(ctx, name, raw)
    assert out.error is None, out.error.body()
    jsonschema.validate(out.body, SCHEMAS[name])


def _every_page(ctx, name, args):
    pages, cursor = [], None
    while True:
        out = tools.call(ctx, name, args | ({"cursor": cursor} if cursor else {}))
        assert out.error is None, out.error.body()
        jsonschema.validate(out.body, SCHEMAS[name])
        pages.append(out.body)
        cursor = out.body.get("next_cursor")
        if cursor is None:
            return pages


def test_todos_past_the_file_cap_say_partial_and_page_on(paths, root, monkeypatch):
    ctx = backend.context(paths, root)
    everything = tools.call(ctx, "list_todos", {"status": "all"}).body["todos"]
    assert {t["date"] for t in everything} == {"2026-10-21", "2026-10-20"}
    monkeypatch.setattr(backend.todos, "FILES_MAX", 1)
    pages = _every_page(ctx, "list_todos", {"status": "all"})
    assert pages[0]["partial"] is True and {t["date"] for t in pages[0]["todos"]} == {"2026-10-21"}
    assert [t for page in pages for t in page["todos"]] == everything


def test_recordings_past_the_scan_cap_say_partial_and_page_on(paths, root, monkeypatch):
    ctx = backend.context(paths, root)
    everything = tools.call(ctx, "list_recordings", {}).body["recordings"]
    assert len(everything) == 6
    monkeypatch.setattr(backend, "SCAN_DIRS", 2)
    pages = _every_page(ctx, "list_recordings", {})
    assert pages[0]["partial"] is True and len(pages[0]["recordings"]) == 2
    assert [r for page in pages for r in page["recordings"]] == everything
    filtered = _every_page(ctx, "list_recordings", {"state": "failed"})
    assert [r["id"] for page in filtered for r in page["recordings"]] == [at.FAILED]


def test_status_sends_only_the_constant_request(paths, root):
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(str(paths.sock))
    listener.listen(1)
    received = []

    def serve():
        conn, _ = listener.accept()
        with conn, conn.makefile("rwb") as stream:
            received.append(stream.readline())
            stream.write(b'{"ok": true, "running": true, "link": "ready", "device": {"serial": "S", "fw": "f"}}\n')
            stream.flush()

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        out = tools.call(backend.context(paths, root), "get_status", {})
    finally:
        thread.join(5)
        listener.close()
    assert received == [b'{"cmd": "status"}\n']
    assert out.body["daemon"] == "running" and "serial" not in out.body["device"]


def test_context_without_writer_when_no_write_tools(paths, root):
    assert backend.context(paths, root).writer is None
    assert backend.context(paths, root, writes=True).writer is not None
