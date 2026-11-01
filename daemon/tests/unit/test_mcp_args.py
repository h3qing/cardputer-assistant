"""Tool argument validation (docs/15 §3.4, §3.6, §3.8): the published input schemas are the checks."""

from __future__ import annotations

from datetime import date

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.mcp import args
from cpad.mcp.args import BadArg
from cpad.mcp.catalog import READ_NAMES, WRITE_NAMES


def _field(name, raw):
    with pytest.raises(BadArg) as err:
        args.parse(name, raw)
    return err.value.field


def test_defaults_fill_in():
    got = args.parse("list_recordings", {})
    assert got["limit"] == 20 and got["from"] is None and got["cursor"] is None and got["mode"] is None


def test_none_arguments_mean_no_arguments():
    assert args.parse("get_status", None) == {}


def test_dates_become_dates():
    got = args.parse("list_days", {"from": "2026-10-01", "to": "2026-10-21"})
    assert got["from"] == date(2026, 10, 1) and got["to"] == date(2026, 10, 21)


def test_session_ids_are_lower_cased():
    assert args.parse("get_recording", {"id": "3QZK1M7-00042-0A1C"})["id"] == "3qzk1m7-00042-0a1c"


def test_filing_list_defaults_to_new_rows_of_this_mac():
    got = args.parse("list_unfiled", {})
    assert (got["filing"], got["orphans"], got["none"], got["limit"], got["project"]) == ("new", False, False, 20, None)
    flags = args.parse("list_unfiled", {"orphans": True, "none": True, "project": [], "tx": ["stale", "failed"]})
    assert (flags["orphans"], flags["none"], flags["tx"]) == (True, True, ("stale", "failed"))


def test_search_scopes_default_to_everything():
    assert args.parse("search", {"query": "x"})["in"] == ("notes", "summaries", "transcripts")
    assert args.parse("search", {"query": "x", "in": ["notes"]})["in"] == ("notes",)


@pytest.mark.parametrize(
    ("name", "raw", "field"),
    [
        ("list_days", {"from": "2026-02-30"}, "from"),
        ("list_days", {"from": "2026-10-21", "to": "2026-10-01"}, "from"),
        ("list_days", {"limit": 0}, "limit"),
        ("list_days", {"limit": 101}, "limit"),
        ("list_days", {"limit": True}, "limit"),
        ("list_days", {"limit": 1.5}, "limit"),
        ("list_days", {"cursor": "../etc"}, "cursor"),
        ("read_daily_note", {}, "date"),
        ("read_daily_note", {"date": "2026-1-2"}, "date"),
        ("read_daily_note", {"date": "2026-10-21", "offset": -1}, "offset"),
        ("get_recording", {"id": "../../config.json"}, "id"),
        ("get_recording", {"id": "3QZK1M7-00042-0A1C\n"}, "id"),
        ("list_recordings", {"mode": "call"}, "mode"),
        ("list_recordings", {"project": "Bad Key"}, "project"),
        ("list_todos", {"status": "maybe"}, "status"),
        ("search", {"query": ""}, "query"),
        ("search", {"query": "x" * 201}, "query"),
        ("search", {"query": "x", "in": ["notes", "notes"]}, "in"),
        ("search", {"query": "x", "in": ["audio"]}, "in"),
        ("list_unfiled", {"filing": "hidden"}, "filing"),
        ("list_unfiled", {"orphans": 1}, "orphans"),
        ("list_unfiled", {"none": "true"}, "none"),
        ("list_unfiled", {"project": "cpa"}, "project"),
        ("list_unfiled", {"project": ["cpa", "cpa"]}, "project"),
        ("list_unfiled", {"project": ["Bad Key"]}, "project"),
        ("list_unfiled", {"tx": ["archived"]}, "tx"),
        ("list_unfiled", {"device": "28:84:85:75"}, "device"),
        ("list_unfiled", {"link": "other"}, "link"),
        ("list_unfiled", {"query": "x" * 201}, "query"),
        ("list_unfiled", {"limit": 51}, "limit"),
        ("get_filing_item", {}, "id"),
        ("get_status", {"anything": 1}, "arguments"),
        ("list_days", "not an object", "arguments"),
    ],
)
def test_bad_arguments_name_the_field(name, raw, field):
    assert _field(name, raw) == field


@pytest.mark.parametrize(
    ("text", "ok"),
    [("hello", "hello"), ("  tidy  ", "tidy"), ("a\tb\nc", "a b c"), ("汉" * 166, "汉" * 166)],
)
def test_queue_text_is_cleaned(text, ok):
    assert args.parse("queue_add", {"project": "cpa", "text": text})["text"] == ok


@pytest.mark.parametrize("text", ["", "   ", "\n\t", "-rf /", "  --help", "汉" * 167])
def test_queue_text_is_refused(text):
    assert _field("queue_add", {"project": "cpa", "text": text}) == "text"


MINIMAL = {"read_daily_note": {"date": "2026-10-21"}, "get_recording": {"id": "3QZK1M7-00042-0A1C"},
           "read_transcript": {"id": "3QZK1M7-00042-0A1C"}, "search": {"query": "x"},
           "queue_add": {"project": "cpa", "text": "t"}, "get_filing_item": {"id": "3QZK1M7-00042-0A1C"}}  # fmt: skip


@pytest.mark.parametrize("name", READ_NAMES + WRITE_NAMES)
def test_every_tool_has_a_parser(name):
    assert set(args.parse(name, MINIMAL.get(name, {}))) == set(args.INPUTS[name]["properties"])


def test_unknown_tool_has_no_parser():
    with pytest.raises(KeyError):
        args.parse("nope", {})


JSON = st.recursive(
    st.none() | st.booleans() | st.integers() | st.floats(allow_nan=False) | st.text(max_size=30),
    lambda inner: st.lists(inner, max_size=4) | st.dictionaries(st.text(max_size=8), inner, max_size=4),
    max_leaves=12,
)
KEYS = st.sampled_from(["from", "to", "limit", "cursor", "date", "offset", "id", "mode", "project", "state",
                        "status", "kind", "query", "in", "text", "x"])  # fmt: skip


@settings(max_examples=300, deadline=None)
@given(st.sampled_from(READ_NAMES + WRITE_NAMES), st.dictionaries(KEYS, JSON, max_size=5) | JSON)
def test_fuzz_never_crashes(name, raw):
    try:
        got = args.parse(name, raw)
    except BadArg:
        return
    assert isinstance(dict(got), dict)
