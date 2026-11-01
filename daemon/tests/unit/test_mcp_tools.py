"""Tool handlers over a fake archive (docs/15 §3.4, §3.6–§3.8): marked, allow-listed, bounded, paged."""

from __future__ import annotations

import json
from datetime import date

import jsonschema
import pytest

from cpad.archive.fs import ArchiveError
from cpad.archive.ids import encode_cursor
from cpad.mcp import tools
from cpad.mcp.catalog import READ_TOOLS, WRITE_TOOLS
from cpad.mcp.limits import OUTPUT_MAX, json_size
from cpad.mcp.tools import Ctx, Keyed
from tests.support.fake_reader import MEETING, MEMO, FakeReader, FakeWriter, fake_status, filing_row

SCHEMAS = {t.name: t.output_schema for t in READ_TOOLS + WRITE_TOOLS}


@pytest.fixture
def reader():
    return FakeReader()


@pytest.fixture
def ctx(reader):
    return Ctx(reader=reader, status=fake_status, writer=FakeWriter())


def _ok(ctx, name, raw=None):
    out = tools.call(ctx, name, raw or {})
    assert out.error is None, out.error and out.error.body()
    jsonschema.validate(out.body, SCHEMAS[name])
    assert out.body["trust"] == "untrusted-user-data"
    return out


def _err(ctx, name, raw=None):
    out = tools.call(ctx, name, raw or {})
    assert out.body is None
    return out.error


def test_list_days_newest_first(ctx):
    out = _ok(ctx, "list_days")
    assert [d["date"] for d in out.body["days"]] == ["2026-10-21", "2026-10-20"]
    assert out.n == 2 and "next_cursor" not in out.body


def test_list_days_pages_with_an_opaque_cursor(ctx, reader):
    first = _ok(ctx, "list_days", {"limit": 1}).body
    assert [d["date"] for d in first["days"]] == ["2026-10-21"] and "truncated" not in first
    second = _ok(ctx, "list_days", {"limit": 1, "cursor": first["next_cursor"]}).body
    assert [d["date"] for d in second["days"]] == ["2026-10-20"] and "next_cursor" not in second
    assert reader.calls[-1] == ("days", ({"from": None, "to": None, "limit": 1, "cursor": first["next_cursor"]},
                                         "2026-10-21"))  # fmt: skip


@pytest.mark.parametrize("cursor", ["garbage", encode_cursor("recordings", "2026-10-21")])
def test_foreign_or_broken_cursor_is_a_bad_argument(ctx, cursor):
    err = _err(ctx, "list_days", {"cursor": cursor})
    assert (err.code, err.field) == ("E_BAD_ARG", "cursor")


def test_filters_reach_the_reader(ctx, reader):
    _ok(ctx, "list_recordings", {"from": "2026-10-01", "to": "2026-10-21", "mode": "meeting", "project": "cpa",
                                 "state": "done"})  # fmt: skip
    asked = reader.calls[-1][1][0]
    assert asked["from"] == date(2026, 10, 1) and asked["mode"] == "meeting" and asked["state"] == "done"


def test_rows_are_allow_listed(ctx):
    out = _ok(ctx, "list_recordings")
    assert all("secret_path" not in r for r in out.body["recordings"])
    captures = _ok(ctx, "list_captures").body["captures"]
    assert "dev" not in captures[0]
    assert "sid" not in _ok(ctx, "list_queue").body["items"][0]


def test_ids_reach_the_audit_but_text_does_not(ctx):
    out = _ok(ctx, "list_recordings", {"from": "2026-10-01", "project": "cpa"})
    assert out.ids == ("2026-10-01", "cpa")
    search = _ok(ctx, "search", {"query": "Bob"})
    assert search.sizes == {"qlen": 3} and search.ids == ()


def test_read_daily_note_pages_by_lines(ctx, reader):
    reader.notes["2026-10-19"] = "".join(f"- 第 {i} 行 " + "汉" * 400 + "\n" for i in range(200))
    first = _ok(ctx, "read_daily_note", {"date": "2026-10-19"}).body
    assert first["truncated"] and first["next_offset"] > 0 and first["total_lines"] == 200
    assert json_size(first) <= OUTPUT_MAX
    second = _ok(ctx, "read_daily_note", {"date": "2026-10-19", "offset": first["next_offset"]}).body
    assert second["text"].startswith(f"- 第 {first['next_offset']} 行")


def test_missing_day_is_not_found(ctx):
    assert _err(ctx, "read_daily_note", {"date": "2020-01-01"}).code == "E_NOT_FOUND"


def test_get_recording_keeps_only_published_fields(ctx):
    body = _ok(ctx, "get_recording", {"id": MEETING.upper()}).body
    assert body["id"] == MEETING.upper() and body["state"] == "done"
    assert body["meta"]["marks"] == [{"t_s": 12.8, "k": "star"}]
    assert "device" not in body["meta"] and body["meta"]["project"] == {"key": "cpa", "name": "Cardputer 助手"}
    assert "pipeline" not in body["result"] and body["result"]["actions"] == [{"n": 1, "text": "发报价单",
                                                                               "confident": True}]  # fmt: skip
    assert "pricing" in body["summary"]["text"]


def test_read_transcript(ctx):
    body = _ok(ctx, "read_transcript", {"id": MEETING}).body
    assert body["total"] == 3 and body["sentences"][2] == {"i": 2, "t_s": 361.2, "text": "交付时间定在周五。"}


@pytest.mark.parametrize("code", ["E_NOT_FOUND", "E_TOO_LARGE", "E_UNSAFE"])
def test_archive_errors_become_tool_errors(ctx, reader, code):
    def boom(*args):
        raise ArchiveError(code)

    reader.transcript = boom
    assert _err(ctx, "read_transcript", {"id": MEETING}).code == code


def test_archive_io_error_is_unavailable(ctx, reader):
    def boom(*args):
        raise ArchiveError("E_IO")

    reader.transcript = boom
    assert _err(ctx, "read_transcript", {"id": MEETING}).code == "E_UNAVAILABLE"


def test_bad_arguments_name_the_field_only(ctx):
    err = _err(ctx, "get_recording", {"id": "../../config.json"})
    assert err.body() == {"error": {"code": "E_BAD_ARG", "message": "An argument is invalid", "field": "id"}}


def test_long_lists_are_cut_to_the_budget_and_resumable(ctx, reader):
    reader.recs = [Keyed(f"2026-10-21/{i:04d}", {"id": f"r{i}", "title": "汉" * 199, "date": "2026-10-21"})
                   for i in range(9999, 9949, -1)]  # fmt: skip
    body = _ok(ctx, "list_recordings", {"limit": 50}).body
    assert body["truncated"] and len(body["recordings"]) < 50 and json_size(body) <= OUTPUT_MAX
    after = tools.decode_cursor("recordings", body["next_cursor"])
    assert after == f"2026-10-21/{9999 - len(body['recordings']) + 1:04d}"


def test_a_partial_listing_says_so_and_resumes_where_the_scan_stopped(ctx, reader):
    row = Keyed("2026-10-21/n/000002", {"text": "t", "done": False, "date": "2026-10-21"})
    reader.todos = lambda a, after: tools.Listing([row], partial=True, resume="2026-10-20/~")
    body = _ok(ctx, "list_todos").body
    assert body["partial"] is True and tools.decode_cursor("todos", body["next_cursor"]) == "2026-10-20/~"
    reader.todos = lambda a, after: tools.Listing([], partial=True)
    body = _ok(ctx, "list_todos").body
    assert body["partial"] is True and "next_cursor" not in body
    assert "partial" not in _ok(ctx, "list_days").body


def test_list_queue_has_no_cursor_but_says_truncated(ctx, reader):
    row = {"project": "cpa", "intent": "todo", "text": "t", "origin": "cli", "uncertain": False}
    reader.queue_rows = [Keyed(f"q{i:010d}", {"qid": f"q{i}", **row}) for i in range(5, 0, -1)]
    body = _ok(ctx, "list_queue", {"limit": 2}).body
    assert len(body["items"]) == 2 and body["truncated"] and "next_cursor" not in body


def test_search_hits_are_bounded_and_a_cut_page_resumes_at_the_first_dropped_hit(ctx, reader):
    reader.notes = {f"2026-09-{d:02d}": ("needle " + "汉" * 2000 + "\n") * 3 for d in range(1, 29)}
    body = _ok(ctx, "search", {"query": "NEEDLE", "limit": 50}).body
    assert body["hits"] and body["truncated"] and not body["partial"] and json_size(body) <= OUTPUT_MAX
    assert all(len(h["snippet"]) <= 300 for h in body["hits"])
    every = reader.search("needle", (), None, None, 50)["hits"]
    assert tools.decode_cursor("hits", body["next_cursor"]) == every[len(body["hits"])]["pos"]


def test_search_passes_its_cursor_on_and_returns_the_next_one(ctx, reader):
    body = _ok(ctx, "search", {"query": "Bob", "cursor": encode_cursor("hits", "2026-10-21/1#1")}).body
    assert reader.calls[-1][1][-1] == "2026-10-21/1#1"
    assert "next_cursor" not in body and body["partial"] is False
    reader.notes = {"2026-10-21": "Bob\nBob\n"}
    first = _ok(ctx, "search", {"query": "bob", "limit": 1}).body
    assert len(first["hits"]) == 1 and tools.decode_cursor("hits", first["next_cursor"]) == "2026-10-21/1#1"


def test_search_tells_how_to_search_and_read_around_a_hit():
    spec = next(t for t in READ_TOOLS if t.name == "search")
    for phrase in ("Simplified Chinese", "read_transcript", "offset", "matches", "next_cursor", "partial"):
        assert phrase in spec.description
    assert "cursor" in spec.input_schema["properties"]


@pytest.mark.parametrize("state", ["transcribed", "archived", "summarized", "DONE"])
def test_unknown_recording_states_are_bad_arguments_not_empty_lists(ctx, state):
    assert _err(ctx, "list_recordings", {"state": state}).field == "state"


def test_recording_states_are_a_published_enum_with_meanings():
    spec = next(t for t in READ_TOOLS if t.name == "list_recordings")
    states = spec.input_schema["properties"]["state"]["enum"]
    assert states == ["done", "transcript", "processing", "waiting", "blocked", "failed", "pulling"]
    assert spec.output_schema["properties"]["recordings"]["items"]["properties"]["state"]["enum"] == states
    assert all(f"{s} =" in spec.description for s in states) and "archived" not in spec.description


def test_get_status(ctx):
    assert _ok(ctx, "get_status").body["daemon"] == "not running"


def test_queue_add_goes_through_the_writer(ctx):
    out = _ok(ctx, "queue_add", {"project": "cpa", "text": "  修一下  "})
    assert out.body == {"trust": "untrusted-user-data", "qid": "q1", "project": "cpa"}
    assert ctx.writer.added == [("cpa", "修一下")]
    assert out.sizes == {"text_len": len("修一下".encode())} and out.ids == ("cpa",)


def test_queue_add_unknown_project(ctx):
    assert _err(ctx, "queue_add", {"project": "nope", "text": "x"}).field == "project"


def test_queue_add_without_writer_is_unavailable(reader):
    ctx = Ctx(reader=reader, status=fake_status)
    assert _err(ctx, "queue_add", {"project": "cpa", "text": "x"}).code == "E_UNAVAILABLE"


def test_oversized_result_is_refused_not_sent(ctx, reader):
    reader.summaries[MEETING] = "汉" * 20_000
    reader.recording = lambda sid, budget: {"summary": {"text": "汉" * 20_000}}
    assert _err(ctx, "get_recording", {"id": MEETING}).code == "E_TOO_LARGE"


def test_rate_limit_error_body_carries_retry_after():
    err = tools.ToolError("E_RATE_LIMITED", retry_after_s=1.5)
    assert err.body()["error"]["retry_after_s"] == 1.5
    assert json.dumps(err.body())


@pytest.mark.parametrize("line", ["", '"', "\t\\", "\x01"])
def test_json_escaping_shrinks_the_page_instead_of_failing(ctx, reader, line):
    reader.notes["2026-10-18"] = (line + "\n") * 30_000
    body = _ok(ctx, "read_daily_note", {"date": "2026-10-18"}).body
    assert json_size(body) <= OUTPUT_MAX and body["truncated"] and body["next_offset"] > 0


def test_a_big_result_leaves_the_summary_what_is_left(ctx, reader):
    def recording(sid, budget):
        actions = [{"n": n, "text": "汉" * 200, "confident": True} for n in range(1, 21)]
        return {"id": sid.upper(), "state": "done", "meta": {"mode": "meeting"},
                "result": {"state": "done", "memo": {"text": "汉" * 2000, "intent": "note"}, "actions": actions},
                "summary": {"text": "汉" * (budget // 3), "truncated": True}}  # fmt: skip

    reader.recording = recording
    body = _ok(ctx, "get_recording", {"id": MEETING}).body
    assert json_size(body) <= OUTPUT_MAX and body["summary"]["truncated"] and body["summary"]["text"]


# --- list_unfiled / get_filing_item (docs/23 §3.5) ------------------------------------------------


def test_list_unfiled_passes_its_arguments_on_and_returns_the_contract_page(ctx, reader):
    body = _ok(ctx, "list_unfiled", {"orphans": True, "query": "报价", "from": "2026-10-20"}).body
    asked = reader.calls[-1][1]
    assert (asked["orphans"], asked["query"], asked["from"]) == (True, "报价", date(2026, 10, 20))
    assert asked["filing"] == "new"
    assert body["v"] == 1 and body["next_cursor"] is None and "truncated" not in body
    assert body["rows"] == reader.unfiled_rows and body["counts"]["new"] == 2


def test_unfiled_rows_keep_only_the_contract_fields(ctx, reader):
    env = {"host": "公司", "link": "here", "net": {"id": "abcdef012345", "kind": "wifi", "when": "record"}}
    reader.unfiled_rows = [filing_row(MEMO.upper(), 1, env=env, dev="020000000001", dir="recordings/x", fw="f",
                                      prof={"name": "家"}, clips=[{"rec": "x"}],
                                      queue={"qid": "q3", "intent": "spawn", "open": True, "text": "x"})]  # fmt: skip
    row = _ok(ctx, "list_unfiled").body["rows"][0]
    assert set(row) == set(filing_row(MEMO, 1)) and row["env"] == {"host": "公司", "link": "here"}
    assert (row["prof"], row["clips"], row["queue"]) == (None, None, {"qid": "q3", "intent": "spawn", "open": True})


def test_a_long_unfiled_page_is_cut_to_the_budget_and_goes_on_after_the_last_row_sent(ctx, reader):
    reader.unfiled_rows = [filing_row(f"3QZK1M7-00042-{n:04d}", 1_000_000 - n, text="汉" * 280, title="题" * 80)
                           for n in range(50)]  # fmt: skip
    body = _ok(ctx, "list_unfiled", {"limit": 50}).body
    assert body["truncated"] and 0 < len(body["rows"]) < 50 and json_size(body) <= OUTPUT_MAX
    last = body["rows"][-1]
    assert tools.decode_cursor("filing", body["next_cursor"]) == f"{last['at']['wall_ms']}/{last['id']}"


def test_an_unfiled_row_too_big_for_any_page_is_refused(ctx, reader):
    reader.unfiled_rows = [filing_row(MEMO.upper(), 1, text="汉" * 9000)]
    assert _err(ctx, "list_unfiled").code == "E_TOO_LARGE"


def test_get_filing_item_never_names_the_firmware(ctx, reader):
    body = _ok(ctx, "get_filing_item", {"id": MEMO.upper()}).body
    assert reader.calls[-1] == ("filing_item", MEMO) and body["fw"] is None and body["audio"] == ["seg-000.wav"]
    assert body["row"] == reader.unfiled_rows[0] and "truncated" not in body


def test_a_long_transcript_is_cut_and_says_where_read_transcript_goes_on(ctx, reader):
    reader.sentences[MEMO] = [{"i": n, "t_s": float(n), "text": f"第 {n} 句。" + "汉" * 60} for n in range(2000)]
    body = _ok(ctx, "get_filing_item", {"id": MEMO}).body
    kept = len(body["sentences"])
    assert body["truncated"] and 0 < kept < 2000 and body["next_offset"] == kept and json_size(body) <= OUTPUT_MAX
    assert body["sentences"][-1]["text"].startswith(f"第 {kept - 1} 句。")


def test_get_filing_item_of_an_unknown_id_is_not_found(ctx):
    assert _err(ctx, "get_filing_item", {"id": MEETING}).code == "E_NOT_FOUND"
