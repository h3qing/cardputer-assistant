"""Archive ids, cursors and paging (docs/15 §3.8): only validated ids ever become path components."""

from __future__ import annotations

from datetime import date

import pytest
from hypothesis import given
from hypothesis import strategies as st

from cpad.archive import ids
from cpad.archive.paging import Page, page


def test_parse_date_accepts_real_dates_only():
    assert ids.parse_date("2026-10-21") == date(2026, 10, 21)


@pytest.mark.parametrize(
    "text", ["2026-02-30", "2026-1-01", "2026-10-21\n", "２026-10-21", "20261021", "", "../2026-10-21", None, 5]
)
def test_parse_date_rejects_everything_else(text):
    with pytest.raises(ids.BadId) as exc:
        ids.parse_date(text)
    assert exc.value.field == "date"


def test_parse_sid_is_case_insensitive_and_returns_lower_case():
    assert ids.parse_sid("3QZK1M7-00042-0A1C") == "3qzk1m7-00042-0a1c"
    assert ids.parse_sid("3qzk1m7-00042-0a1c") == "3qzk1m7-00042-0a1c"


@pytest.mark.parametrize(
    "text",
    ["3QZK1M7-00042-0A1C\n", "3QZK1M7-00042-0AIC", "3QZK1M7-00042-0A1", "3QZK1M7-00042-0A1C", "../x", "", None],
)
def test_parse_sid_rejects_non_monoids(text):
    with pytest.raises(ids.BadId):
        ids.parse_sid(text)


def test_split_rec_dir():
    assert ids.split_rec_dir("1402-3qzk1m7-00042-0a1c") == ("1402", "3qzk1m7-00042-0a1c")


@pytest.mark.parametrize(
    "name", ["1402-3QZK1M7-00042-0A1C", "140-3qzk1m7-00042-0a1c", "1402_3qzk1m7-00042-0a1c", "1402-x", ".1402-x", ""]
)
def test_split_rec_dir_rejects_other_names(name):
    assert ids.split_rec_dir(name) is None


def test_cursor_round_trip_and_kind_check():
    cursor = ids.encode_cursor("days", "2026-10-21")
    assert ids.decode_cursor("days", cursor) == "2026-10-21"
    assert "2026" not in cursor  # opaque
    with pytest.raises(ids.BadId) as exc:
        ids.decode_cursor("recordings", cursor)
    assert exc.value.field == "cursor"


@given(st.one_of(st.text(max_size=600), st.none(), st.integers()))
def test_decode_cursor_never_crashes(text):
    try:
        ids.decode_cursor("days", text)
    except ids.BadId:
        pass


def test_decode_cursor_rejects_oversized_input():
    with pytest.raises(ids.BadId):
        ids.decode_cursor("days", ids.encode_cursor("days", "x" * 600))


def test_in_range():
    assert ids.in_range("2026-10-21", None, None)
    assert ids.in_range("2026-10-21", date(2026, 10, 21), date(2026, 10, 21))
    assert not ids.in_range("2026-10-21", date(2026, 10, 22), None)
    assert not ids.in_range("2026-10-21", None, date(2026, 10, 20))


def test_page_takes_items_strictly_after_the_key_newest_first():
    items = ("e", "d", "c", "b", "a")
    first = page(items, str, None, 2)
    assert first == Page(("e", "d"), "d")
    assert page(items, str, first.next_key, 2) == Page(("c", "b"), "b")
    assert page(items, str, "b", 2) == Page(("a",), None)
    assert page(items, str, "a", 2) == Page((), None)
    assert page(items, str, "cc", 5).items == ("c", "b", "a")  # the key's own item vanished
