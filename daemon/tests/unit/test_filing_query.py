"""The filters of `cpa filing ls` (docs/23 §3.1) and the argument rules of §3.3 rule 1: ids, project keys and enum
words are validated first; an error names the argument (field), never echoes its value. Pure."""

from __future__ import annotations

from datetime import date

import pytest

from cpad.archive import ids
from cpad.filing import args, query
from cpad.filing import fold as ff
from cpad.projects.registry import Project

KNOWN = ff.Known.of((Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b")))
A, B, C, D = "3QZK1M7-0000A-0001", "3QZK1M7-0000A-0002", "3QZK1M7-0000A-0003", "3QZK1M7-0000A-0004"


def row(rec: str, wall_ms: int, **over) -> dict:
    return {"id": rec, "kind": "memo", "filing": "new", "why": ["unfiled"], "asked": None,
            "at": {"wall_ms": wall_ms, "tz_min": None, "uncertain": False}, "dur_ms": 1000, "device": "0a1b2c3d4e5f",
            "env": None, "prof": None, "project": None, "starred": False, "tx": "waiting", "title": "标题",
            "text": "正文", "intent": None, "clips": None, "queue": None, **over}  # fmt: skip


def entry(rec: str, wall_ms: int, day: str | None = "2026-10-22", **over) -> query.Entry:
    return query.Entry(row(rec, wall_ms, **over), day)


ENTRIES = (
    entry(A, 3000),
    entry(B, 2000, filing="filed", why=[], project="cpa", kind="meeting", tx="done", day="2026-10-21"),
    entry(C, 1000, asked="acme", why=["orphan"], env={"host": "公司", "link": "here"}, device="ffffffffffff"),
    entry(D, 4000, filing="hidden", why=[], tx="failed", day=None),
)


def picked(raw: dict, entries=ENTRIES) -> list[str]:
    return [e.row["id"] for e in query.candidates(entries, query.parse(raw, KNOWN))]


# --- arguments ------------------------------------------------------------------------------------


def test_ids_are_monoids_in_any_case_and_come_back_upper_case():
    assert args.parse_id("3qzk1m7-0000a-0001") == A
    for bad in ("", "3QZK1M7-0000A", "../3QZK1M7-0000A-0001", "3QZK1M7-0000A-0001\n", None, 7, "Ｑ" * 18):
        with pytest.raises(args.FilingError) as err:
            args.parse_id(bad)
        assert (err.value.code, err.value.field) == ("E_BAD_ARG", "id")


def test_keys_follow_the_registry_pattern():
    assert args.parse_key("cpa-2") == "cpa-2"
    for bad in ("CPA", "-cpa", "a" * 13, "c p", "", None):
        with pytest.raises(args.FilingError) as err:
            args.parse_key(bad)
        assert (err.value.code, err.value.field) == ("E_BAD_ARG", "project")


def test_an_error_is_a_fixed_code_a_field_and_an_exit_code_never_the_value():
    err = args.FilingError("E_BAD_ARG", "id")
    assert err.doc() == {"ok": False, "v": 1, "error": "E_BAD_ARG", "field": "id"} and err.exit_code == 2
    assert args.FilingError("E_UNKNOWN_PROJECT", "project").exit_code == 2
    assert args.FilingError("E_UNKNOWN_ID", "id").exit_code == 1
    assert args.FilingError("E_UNAVAILABLE").doc() == {"ok": False, "v": 1, "error": "E_UNAVAILABLE"}
    assert str(err) == "E_BAD_ARG (id)"


@pytest.mark.parametrize(
    ("raw", "field", "code"),
    [
        ({"filing": "archived"}, "filing", "E_BAD_ARG"),
        ({"from": "2026-13-01"}, "from", "E_BAD_ARG"),
        ({"to": "yesterday"}, "to", "E_BAD_ARG"),
        ({"from": "2026-10-22", "to": "2026-10-21"}, "to", "E_BAD_ARG"),
        ({"device": "02:00:00:00:00:01"}, "device", "E_BAD_ARG"),
        ({"link": "elsewhere"}, "link", "E_BAD_ARG"),
        ({"mode": "call"}, "mode", "E_BAD_ARG"),
        ({"project": ["Cpa"]}, "project", "E_BAD_ARG"),
        ({"project": ["acme"]}, "project", "E_UNKNOWN_PROJECT"),
        ({"project": ["cpa"], "none": True}, "project", "E_BAD_ARG"),
        ({"tx": ["sleeping"]}, "tx", "E_BAD_ARG"),
        ({"text": ""}, "text", "E_BAD_ARG"),
        ({"text": "长" * 201}, "text", "E_BAD_ARG"),
        ({"limit": "0"}, "limit", "E_BAD_ARG"),
        ({"limit": "1001"}, "limit", "E_BAD_ARG"),
        ({"limit": "ten"}, "limit", "E_BAD_ARG"),
        ({"limit": True}, "limit", "E_BAD_ARG"),
        ({"cursor": "!!"}, "cursor", "E_BAD_ARG"),
        ({"cursor": ids.encode_cursor("filing", "not a position")}, "cursor", "E_BAD_ARG"),
        ({"cursor": ids.encode_cursor("recordings", f"1/{A}")}, "cursor", "E_BAD_ARG"),
    ],
)
def test_bad_filters_name_their_argument(raw, field, code):
    with pytest.raises(args.FilingError) as err:
        query.parse(raw, KNOWN)
    assert (err.value.code, err.value.field) == (code, field)


def test_the_defaults_list_new_rows_of_this_mac():
    q = query.parse({}, KNOWN)
    assert (q.filing, q.orphans, q.limit, q.text, q.after) == ("new", False, query.LIMIT_DEFAULT, None, None)
    assert query.parse({"limit": 5, "from": "2026-10-01"}, KNOWN).start == date(2026, 10, 1)


# --- filters --------------------------------------------------------------------------------------


def test_new_rows_of_this_mac_newest_first_and_the_other_mac_group_only_when_asked():
    assert picked({}) == [A]
    assert picked({"orphans": True}) == [A, C]


def test_all_is_every_state_but_hidden_and_hidden_is_its_own_filter():
    assert picked({"filing": "all", "orphans": True}) == [A, B, C]
    assert picked({"filing": "hidden"}) == [D]
    assert picked({"filing": "filed"}) == [B]


@pytest.mark.parametrize(
    ("raw", "want"),
    [
        ({"from": "2026-10-22"}, [A, C]),
        ({"to": "2026-10-21"}, [B]),
        ({"from": "2026-10-21", "to": "2026-10-21"}, [B]),
        ({"device": "ffffffffffff"}, [C]),
        ({"link": "here"}, [C]),
        ({"link": "unknown"}, [A, B]),
        ({"mode": "meeting"}, [B]),
        ({"project": ["cpa", "blog"]}, [B]),
        ({"none": True}, [A, C]),
        ({"tx": ["done", "failed"]}, [B]),
    ],
)
def test_each_filter(raw, want):
    assert picked({"filing": "all", "orphans": True, **raw}) == want


def test_a_row_without_a_day_is_left_out_by_a_date_filter():
    assert picked({"filing": "hidden", "from": "2000-01-01"}) == []


def test_text_is_a_literal_case_insensitive_substring():
    assert query.contains(("Pricing 方案", None), "pRICING")
    assert query.contains(("a.*b",), ".*") and not query.contains(("ab",), ".*")
    assert not query.contains((None, ""), "x")


# --- pages ----------------------------------------------------------------------------------------


def test_pages_follow_the_cursor_and_skip_rows_that_do_not_match_the_text():
    entries = tuple(entry(f"3QZK1M7-0000B-000{n}", n * 1000) for n in range(1, 8))
    q = query.parse({"limit": "2"}, KNOWN)
    first, cursor = query.page(query.candidates(entries, q), q, lambda e: True)
    assert [e.row["at"]["wall_ms"] for e in first] == [7000, 6000] and cursor
    q2 = query.parse({"limit": "2", "cursor": cursor}, KNOWN)
    second, cursor2 = query.page(query.candidates(entries, q2), q2, lambda e: e.row["at"]["wall_ms"] != 4000)
    assert [e.row["at"]["wall_ms"] for e in second] == [5000, 3000]
    q3 = query.parse({"limit": "2", "cursor": cursor2}, KNOWN)
    third, cursor3 = query.page(query.candidates(entries, q3), q3, lambda e: True)
    assert [e.row["at"]["wall_ms"] for e in third] == [2000, 1000] and cursor3 is None


def test_a_full_last_page_has_no_cursor():
    q = query.parse({"limit": "1", "orphans": True, "filing": "all"}, KNOWN)
    found, cursor = query.page(query.candidates(ENTRIES, q), q, lambda e: e.row["id"] == B)
    assert [e.row["id"] for e in found] == [B] and cursor is None


def test_counts_are_over_every_row_whatever_the_filters():
    found = query.counts([e.row for e in ENTRIES], on_device=None)
    assert found == {"new": 1, "other_mac": 1, "clips_waiting": 0, "on_device": None}


def test_no_text_drops_the_title_and_text_only():
    shown = query.shown(row(A, 1), query.parse({"no_text": True}, KNOWN))
    assert (shown["title"], shown["text"], shown["id"]) == (None, None, A)
    assert query.shown(row(A, 1), query.parse({}, KNOWN))["text"] == "正文"
