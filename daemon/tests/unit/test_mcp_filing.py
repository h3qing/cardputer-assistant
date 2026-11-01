"""MCP list_unfiled and get_filing_item (docs/23 §3.5) over a temporary $CPA_HOME and archive (tests/support/
filing_home): the rows and filters of `cpa filing ls` / `show`, read only, never a hidden recording, the other-Mac
group only on request, never the device's own id, a path, the firmware or a network. Also the §3.5 fixes:
list_queue leaves out items handed from hidden recordings; list_recordings and get_recording carry env{host, link}."""

from __future__ import annotations

import json

import jsonschema
import pytest

from cpad.archive.fs import Root
from cpad.config import host
from cpad.config.settings import load_config
from cpad.events import records as queue_records
from cpad.events.log import EventLog
from cpad.filing import view
from cpad.filing.args import FilingError
from cpad.filing.rows import device_id
from cpad.mcp import backend, catalog, tools
from cpad.recordings import records as r
from tests.support import archive_tree as at
from tests.support import filing_home

AT, DEV, SECRET_ROOT = filing_home.AT, filing_home.DEV, filing_home.SECRET_ROOT
SCHEMAS = {t.name: t.output_schema for t in catalog.READ_TOOLS}
NET = {"id": "abcdef012345", "kind": "wifi", "when": "record"}


@pytest.fixture
def paths(short_tmp, tmp_path):
    return filing_home.build(short_tmp / "home", tmp_path / "notes")


@pytest.fixture
def root(paths):
    return Root.at(load_config(paths.config).notes_path())


@pytest.fixture
def ctx(paths, root):
    return backend.context(paths, root)


def _ok(ctx, name, raw=None) -> dict:
    out = tools.call(ctx, name, raw or {})
    assert out.error is None, out.error.body()
    jsonschema.validate(out.body, SCHEMAS[name])
    assert out.body["trust"] == "untrusted-user-data"
    return out.body


def _err(ctx, name, raw) -> tuple[str, str | None]:
    out = tools.call(ctx, name, raw)
    assert out.body is None
    return out.error.code, out.error.field


def _ids(body: dict) -> list[str]:
    return [row["id"] for row in body["rows"]]


def _stamp(paths, rec: str, env: dict) -> None:
    """Archive `rec` again with a Mac-side stamp (P1-D1's env): the last rec.archived of a session counts."""
    archived = next(e for e in EventLog(paths.events).load() if e["ev"] == "rec.archived" and e["rec"] == rec)
    EventLog(paths.events).append(({**archived, "env": env},))


# --- list_unfiled --------------------------------------------------------------------------------


def test_list_unfiled_is_cpa_filing_ls(paths, root, ctx):
    body = _ok(ctx, "list_unfiled")
    assert _ids(body) == [at.MEMO, at.OLD]
    assert body["counts"] == {"new": 2, "other_mac": 1, "clips_waiting": 0, "on_device": None}
    same = view.listing(paths, root, {}, None)
    assert {k: v for k, v in body.items() if k != "trust"} == same


def test_the_other_mac_group_only_with_orphans(ctx):
    assert at.FAILED not in _ids(_ok(ctx, "list_unfiled"))
    body = _ok(ctx, "list_unfiled", {"orphans": True})
    assert _ids(body) == [at.MEMO, at.FAILED, at.OLD]
    failed = body["rows"][1]
    assert (failed["asked"], failed["project"], failed["why"]) == ("acme", None, ["orphan", "no_text"])


def test_hidden_recordings_are_never_listed_or_shown(ctx):
    every = _ok(ctx, "list_unfiled", {"filing": "all", "orphans": True})
    assert set(_ids(every)) == {at.MEETING, at.MEMO, at.OLD, at.FAILED}
    assert _err(ctx, "list_unfiled", {"filing": "hidden"}) == ("E_BAD_ARG", "filing")
    assert _err(ctx, "get_filing_item", {"id": at.BLOCKED}) == ("E_NOT_FOUND", None)


@pytest.mark.parametrize(
    ("raw", "ids"),
    [
        ({"filing": "filed"}, [at.MEETING]),
        ({"filing": "all", "project": ["cpa"]}, [at.MEETING]),
        ({"filing": "all", "none": True}, [at.MEMO, at.OLD]),
        ({"mode": "memo"}, [at.MEMO]),
        ({"from": "2026-10-21"}, [at.MEMO]),
        ({"to": "2026-10-20"}, [at.OLD]),
        ({"tx": ["transcript"]}, [at.OLD]),
        ({"query": "旧会议"}, [at.OLD]),
        ({"query": "ALICE"}, [at.MEMO]),
        ({"link": "unknown"}, [at.MEMO, at.OLD]),
        ({"link": "here"}, []),
        ({"device": "000000000000"}, []),
    ],
)
def test_filters_are_those_of_cpa_filing_ls(ctx, raw, ids):
    assert _ids(_ok(ctx, "list_unfiled", raw)) == ids


def test_the_device_filter_takes_the_short_id(paths, ctx):
    key = host.id_key(paths.id_key)  # as cpad does at start
    body = _ok(ctx, "list_unfiled", {"device": device_id(key, DEV)})
    assert _ids(body) == [at.MEMO, at.OLD] and {row["device"] for row in body["rows"]} == {device_id(key, DEV)}


@pytest.mark.parametrize(
    ("raw", "field"),
    [
        ({"project": ["acme"]}, "project"),
        ({"project": ["cpa"], "none": True}, "project"),
        ({"project": "cpa"}, "project"),
        ({"device": "ABCDEF012345"}, "device"),
        ({"tx": ["nope"]}, "tx"),
        ({"orphans": "yes"}, "orphans"),
        ({"query": ""}, "query"),
        ({"cursor": "garbage"}, "cursor"),
        ({"from": "2026-10-21", "to": "2026-10-20"}, "from"),
        ({"limit": 51}, "limit"),
    ],
)
def test_bad_filters_name_the_argument_only(ctx, raw, field):
    assert _err(ctx, "list_unfiled", raw) == ("E_BAD_ARG", field)


@pytest.mark.parametrize(
    ("code", "field", "error"),
    [("E_UNKNOWN_ID", "id", ("E_NOT_FOUND", None)), ("E_UNKNOWN_PROJECT", "project", ("E_BAD_ARG", "project")),
     ("E_BAD_ARG", "text", ("E_BAD_ARG", "query")), ("E_ALREADY", "queue", ("E_UNAVAILABLE", None))],
)  # fmt: skip
def test_filing_errors_in_mcp_words(code, field, error):
    refused = backend._refused(FilingError(code, field))
    assert (refused.code, refused.field) == error


def test_pages_follow_the_cursor(ctx):
    every = _ids(_ok(ctx, "list_unfiled", {"filing": "all", "orphans": True}))
    seen, cursor = [], None
    while True:
        more = {"cursor": cursor} if cursor else {}
        body = _ok(ctx, "list_unfiled", {"filing": "all", "orphans": True, "limit": 1} | more)
        seen += _ids(body)
        cursor = body["next_cursor"]
        if cursor is None:
            break
    assert seen == every and len(seen) == 4


def test_rows_carry_the_mac_and_link_but_never_a_network(paths, ctx):
    _stamp(paths, at.MEMO, {"host": "公司", "link": "here", "net": NET})
    body = _ok(ctx, "list_unfiled", {"link": "here"})
    assert _ids(body) == [at.MEMO] and body["rows"][0]["env"] == {"host": "公司", "link": "here"}
    assert "abcdef012345" not in json.dumps(body)


# --- get_filing_item -----------------------------------------------------------------------------


@pytest.mark.parametrize("rec", [at.MEETING, at.MEMO, at.OLD, at.FAILED])
def test_get_filing_item_is_cpa_filing_show_without_the_firmware(paths, root, ctx, rec):
    body = _ok(ctx, "get_filing_item", {"id": rec.lower()})
    assert {k: v for k, v in body.items() if k != "trust"} == {**view.detail(paths, root, rec), "fw": None}


def test_get_filing_item_has_the_whole_text_and_every_sentence(ctx):
    memo = _ok(ctx, "get_filing_item", {"id": at.MEMO})
    assert memo["text"] == "周五前给 Alice 发报价单。 第二行" and memo["row"]["filing"] == "new"
    meeting = _ok(ctx, "get_filing_item", {"id": at.MEETING})
    assert meeting["sentences"][0]["text"] == "我们先过一下 pricing 的方案。" and "truncated" not in meeting


@pytest.mark.parametrize(
    ("raw", "error"),
    [
        ({"id": "3QZK1M7-00099-0000"}, ("E_NOT_FOUND", None)),
        ({"id": at.PULLING}, ("E_NOT_FOUND", None)),
        ({"id": "../../config.json"}, ("E_BAD_ARG", "id")),
        ({}, ("E_BAD_ARG", "id")),
    ],
)
def test_unknown_or_bad_ids(ctx, raw, error):
    assert _err(ctx, "get_filing_item", raw) == error


def test_nothing_names_the_device_a_path_or_the_firmware(paths, root, ctx):
    host.id_key(paths.id_key)
    bodies = [_ok(ctx, "list_unfiled", {"filing": "all", "orphans": True})]
    bodies += [_ok(ctx, "get_filing_item", {"id": rec}) for rec in (at.MEETING, at.MEMO, at.OLD, at.FAILED)]
    text = json.dumps(bodies, ensure_ascii=False)
    for secret in (DEV, SECRET_ROOT, str(root.path), "recordings/", "/seg-", "cpa-fw"):
        assert secret not in text


def test_no_tool_files_hands_or_hides(ctx):
    names = set(catalog.READ_NAMES) | set(catalog.WRITE_NAMES)
    assert {"list_unfiled", "get_filing_item"} <= set(catalog.READ_NAMES)
    assert not names & {"file_item", "set_filing_state", "hand_item", "hide_item"}
    assert not any(t.write for t in catalog.READ_TOOLS)


# --- the §3.5 fixes ------------------------------------------------------------------------------


def _hand(paths, qid: str, rec: str) -> None:
    EventLog(paths.events).append((queue_records.queue_handed(qid=qid, project="cpa", text=f"交给 AI {qid}",
                                                              intent="todo", rec=rec, at=AT),))  # fmt: skip


def test_list_queue_leaves_out_items_handed_from_hidden_recordings(paths, ctx):
    _hand(paths, "q1", at.MEMO)
    _hand(paths, "q2", at.OLD)
    assert [i["qid"] for i in _ok(ctx, "list_queue")["items"]] == ["q2", "q1"]
    EventLog(paths.events).append((r.hidden(rec=at.MEMO, at=AT),))
    shown = _ok(ctx, "list_queue")["items"]
    assert [i["qid"] for i in shown] == ["q2"] and "q1" not in json.dumps(shown)
    EventLog(paths.events).append((r.unhidden(rec=at.MEMO, at=AT),))
    assert [i["qid"] for i in _ok(ctx, "list_queue")["items"]] == ["q2", "q1"]


def test_list_recordings_and_get_recording_carry_the_mac_stamp_only(paths, ctx):
    _stamp(paths, at.MEETING, {"host": "家", "link": "here", "net": NET})
    rows = {row["id"].upper(): row for row in _ok(ctx, "list_recordings")["recordings"]}
    assert rows[at.MEETING]["env"] == {"host": "家", "link": "here"} and rows[at.MEMO]["env"] is None
    meta = _ok(ctx, "get_recording", {"id": at.MEETING})["meta"]
    assert meta["env"] == {"host": "家", "link": "here"} and "abcdef012345" not in json.dumps(meta)
