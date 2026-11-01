"""history.page (docs/13 §8.1, §12.2): the recordings app's pages, newest first by wall time. Pure."""

from __future__ import annotations

import dataclasses
import re

import pytest

from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as rm
from cpad.protocol.file_messages import At, FileItem
from cpad.protocol.limits import LIMITS
from cpad.recordings import history, index, sessions
from cpad.recordings import records as r
from cpad.recordings.wall import Seen, Wall

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

DEV = "020000000001"
AT = "2026-10-21T06:45:10.000+00:00"
HOUR = 3_600_000
T0 = 1792440000000
CURSOR = re.compile(LIMITS["rec_cursor_pattern"])
# Before an NVS erase the device's MonoId epoch was 3QZK1M7; after it, the random new epoch 2B7XW4R sorts
# lower, so today's sessions would land on the last page if cpad sorted by session id (docs/13 §8.1).
OLD_1, OLD_2 = "3QZK1M7-00009-0001", "3QZK1M7-00009-0003"
NEW_1, NEW_2 = "2B7XW4R-00002-0001", "2B7XW4R-00003-0001"


def sid(i: int) -> str:
    return f"3QZK1M7-0000A-{i:04d}"


def archived(rec: str, started_ms: int, *, mode: str = "meeting", project: str | None = None,
             dur_ms: int = 2520000) -> dict:  # fmt: skip
    return r.archived(dev=DEV, rec=rec, dir=f"recordings/2026-10-21/1402-{rec.lower()}", mode=mode, project=project,
                      segments=1, dur_ms=dur_ms, started_ms=started_ms, uncertain=False, gaps=[], consent=None,
                      at=AT)  # fmt: skip


def stored(rec: str, wall_ms: int, *, mode: str = "meeting", stars=(), seg: int = 0,
           project: str | None = None) -> dict:  # fmt: skip
    item = FileItem(id=f"{rec[:-2]}S{seg}", kind="rec", size=3244, rec=rec, seg=seg, last=True, mode=mode, dur_ms=100,
                    at=At(1000), stars=tuple(stars), project=project)  # fmt: skip
    return r.seg_stored(dev=DEV, item=item, file=f"recordings/2026-10-21/1402-{rec.lower()}/seg-{seg:03d}.wav",
                        file_sha256="0" * 64, wall=Wall(wall_ms, False), repaired=False, fw=None, at=AT)  # fmt: skip


def result(session: dict, *, title=None, memo_text=None, memo_intent=None, actions_n=0, state="done",
           level="summary") -> dict:  # fmt: skip
    return r.result(rec=session["rec"], state=state, level=level, title=title, memo_text=memo_text,
                    memo_intent=memo_intent, actions_n=actions_n, error_code=None, session=session, at=AT)  # fmt: skip


def on_device(rec: str, wall_ms: int | None, mode: str = "memo", project: str | None = None) -> FileItem:
    return FileItem(id=rec, kind="rec", size=3244, rec=rec, seg=0, last=True, mode=mode, dur_ms=4000,
                    at=At(5000, wall_ms), project=project)  # fmt: skip


def page(events, listing=(), *, cursor=None, max=6, filter="all", seen=None):
    idx = index.fold(events)
    found = sessions.rows(idx, statuses={}, listing=listing, seen=seen)
    return history.page(found, idx, cursor=cursor, max=max, filter=filter)


def recs(items) -> list[str]:
    return [row.rec for row in items]


def many(n: int) -> list[dict]:
    return [archived(sid(i), T0 + i * HOUR) for i in range(n)]


# --- order and paging ---------------------------------------------------------------------


def test_two_monoid_epochs_come_out_in_wall_time_order():
    events = [archived(OLD_1, T0), archived(OLD_2, T0 + HOUR), archived(NEW_1, T0 + 2 * HOUR),
              archived(NEW_2, T0 + 3 * HOUR)]  # fmt: skip
    items, nxt = page(events)
    assert recs(items) == [NEW_2, NEW_1, OLD_2, OLD_1] and nxt is None
    assert [row.wall_ms for row in items] == [T0 + 3 * HOUR, T0 + 2 * HOUR, T0 + HOUR, T0]


def test_equal_times_fall_back_to_the_session_id():
    items, _ = page([archived(OLD_2, T0), archived(OLD_1, T0)])
    assert recs(items) == [OLD_1, OLD_2]


def test_a_cursor_walks_every_session_once():
    events = many(14)
    first, cursor = page(events)
    second, cursor2 = page(events, cursor=cursor)
    third, last = page(events, cursor=cursor2)
    assert [len(first), len(second), len(third), last] == [6, 6, 2, None]
    assert recs(first + second + third) == [sid(i) for i in reversed(range(14))]


def test_paging_is_stable_when_a_new_session_arrives_between_pages():
    events = many(8)
    first, cursor = page(events)
    newer = [*events, archived(NEW_2, T0 + 99 * HOUR), archived(NEW_1, T0 + 98 * HOUR)]
    second, nxt = page(newer, cursor=cursor)
    assert recs(second) == [sid(1), sid(0)] and nxt is None
    assert recs(page(newer)[0])[:2] == [NEW_2, NEW_1]


def test_a_page_of_exactly_max_rows_has_no_next():
    items, nxt = page(many(6))
    assert len(items) == 6 and nxt is None
    assert page(many(7), max=3)[1] is not None


def test_the_cursor_is_opaque_short_and_survives_its_session_being_hidden():
    events = many(9)
    first, cursor = page(events)
    assert CURSOR.fullmatch(cursor) and len(cursor) <= LIMITS["rec_cursor_max"]
    hidden = [*events, r.hidden(rec=first[-1].rec, at=AT)]
    assert recs(page(hidden, cursor=cursor)[0]) == [sid(2), sid(1), sid(0)]


BAD_CURSORS = [
    "x",
    "t1792440000000-a1",
    "t" + "z" * 12 + "_3QZK1M70000A0001",
    "t1_3QZK1M7",
    "u_" + "Z" * 17,
    "t" + "z" * 11 + "_3QZK1M70000A0001",  # past 2^53
]


@pytest.mark.parametrize("bad", BAD_CURSORS)
def test_a_cursor_cpad_did_not_make_is_refused(bad):
    with pytest.raises(history.BadCursor):
        page(many(3), cursor=bad)


# --- rows -----------------------------------------------------------------------------------


def test_sessions_still_on_the_device_are_listed_and_an_unknown_time_sorts_first():
    listing = (on_device("2B7XW4R-00004-0001", T0 + 5 * HOUR), on_device("2B7XW4R-00004-0002", None),
               on_device("2B7XW4R-00004-0003", T0 - HOUR, mode="meeting", project="cpa"))  # fmt: skip
    items, _ = page([archived(OLD_1, T0)], listing)
    assert recs(items) == ["2B7XW4R-00004-0002", "2B7XW4R-00004-0001", OLD_1, "2B7XW4R-00004-0003"]
    assert items[0] == rm.RecRow(rec="2B7XW4R-00004-0002", mode="memo", st="on_device", dur_ms=4000)
    assert items[3].project == "cpa" and items[3].st == "on_device" and items[3].title == "会议 1m"


def test_a_session_without_a_wall_time_keeps_its_place_when_it_lands_between_pages():
    """Recorded before time.set in the boot the link talks to now, it is listed with the start it will land with
    (wall.resolve_wall case 2), so landing does not move it behind a cursor the device already has."""
    boot, start = sid(0)[:13], T0 + 2 * HOUR + HOUR // 2
    x = f"{boot}-0100"
    seen = Seen(boot, up_ms=5000 + 3 * 24 * HOUR, wall_ms=start + 3 * 24 * HOUR)  # on_device() records at up 5000
    events = many(8)
    first, cursor = page(events, (on_device(x, None),), seen=seen)
    assert recs(first) == [sid(7), sid(6), sid(5), sid(4), sid(3), x] and first[-1].wall_ms == start
    second, nxt = page([*events, archived(x, start, mode="memo")], cursor=cursor)
    assert recs(second) == [sid(2), sid(1), sid(0)] and nxt is None
    other_boot = Seen("2B7XW4R-00004", up_ms=seen.up_ms, wall_ms=seen.wall_ms)
    assert recs(page(events, (on_device(x, None),), seen=other_boot)[0])[0] == x, "unknown: still first"


def test_landed_sessions_without_a_manifest_use_their_first_segment():
    items, _ = page([stored(OLD_1, T0 + HOUR, stars=(100, 200))])
    assert items[0].rec == OLD_1 and items[0].st == "pulling" and items[0].wall_ms == T0 + HOUR
    assert items[0].marks == 2


def test_a_tag_changed_after_segment_0_wins_before_the_manifest_too():  # #46
    set_later = [stored(OLD_1, T0), stored(OLD_1, T0, seg=1, project="cpa")]
    cleared = [stored(NEW_1, T0, project="cpa"), stored(NEW_1, T0, seg=1)]
    items = {row.rec: row for row in page([*set_later, *cleared])[0]}
    assert (items[OLD_1].project, items[NEW_1].project) == ("cpa", None)
    x = "2B7XW4R-00004-0001"
    later = dataclasses.replace(on_device(x, T0, mode="meeting"), id="2B7XW4R-00004-0002", seg=1, project="cpa")
    assert page([], (on_device(x, T0, mode="meeting"), later))[0][0].project == "cpa"


def test_a_wall_time_outside_the_protocol_range_is_sent_as_unknown_but_still_sorts():
    items, _ = page([archived(OLD_1, 1000), archived(OLD_2, T0)])
    assert recs(items) == [OLD_2, OLD_1] and items[1].wall_ms is None


def test_state_duration_marks_and_project_come_through():
    meeting = archived(OLD_1, T0, project="cpa", dur_ms=2530000)
    items, _ = page([stored(OLD_1, T0, stars=(5,)), stored(OLD_1, T0, stars=(9, 12), seg=1), meeting])
    assert items[0] == rm.RecRow(rec=OLD_1, mode="meeting", st="archived", wall_ms=T0, dur_ms=2530000, project="cpa",
                                 marks=3, title="会议 42m")  # fmt: skip


def test_dur_and_marks_are_clamped_to_the_protocol_range():
    long = archived(OLD_1, T0, dur_ms=2**33)
    items, _ = page([long, *(stored(OLD_1, T0, stars=range(40000), seg=k) for k in range(2))])
    assert (items[0].dur_ms, items[0].marks) == (2**32 - 1, 65535)


# --- filters, edits, hidden -----------------------------------------------------------------


def _mixed() -> list[dict]:
    memo = archived(OLD_1, T0, mode="memo")
    meeting = archived(OLD_2, T0 + HOUR)
    other = archived(NEW_1, T0 + 2 * HOUR, mode="memo")
    edits = [r.edited_star(rec=OLD_2, starred=True, at=AT), r.edited_project(rec=NEW_1, project="blog", at=AT)]
    return [memo, meeting, other, *edits]


@pytest.mark.parametrize(("tab", "expected"), [("all", [NEW_1, OLD_2, OLD_1]), ("starred", [OLD_2]),
                                               ("memo", [NEW_1, OLD_1]), ("meeting", [OLD_2])])  # fmt: skip
def test_each_filter_returns_its_rows(tab, expected):
    assert recs(page(_mixed(), filter=tab)[0]) == expected


def test_star_and_project_come_from_the_folded_events():
    items = {row.rec: row for row in page(_mixed())[0]}
    assert items[OLD_2].starred is True and items[OLD_1].starred is None
    assert items[NEW_1].project == "blog"
    undo = [r.edited_star(rec=OLD_2, starred=False, at=AT), r.edited_project(rec=NEW_1, project=None, at=AT)]
    unstarred = [*_mixed(), *undo]
    items = {row.rec: row for row in page(unstarred)[0]}
    assert items[OLD_2].starred is None and items[NEW_1].project is None


def test_hidden_sessions_are_left_out_of_every_tab_until_unhidden():
    hidden = [*_mixed(), r.hidden(rec=OLD_2, at=AT)]
    for tab in ("all", "starred", "meeting"):
        assert OLD_2 not in recs(page(hidden, filter=tab)[0])
    assert OLD_2 in recs(page([*hidden, r.unhidden(rec=OLD_2, at=AT)])[0])


# --- titles and todo_n ----------------------------------------------------------------------


def _titled(**kw) -> rm.RecRow:
    session = archived(OLD_1, T0, mode=kw.pop("mode", "meeting"), dur_ms=kw.pop("dur_ms", 2520000))
    return page([session, result(session, **kw)])[0][0]


def test_the_title_comes_from_the_result():
    assert _titled(title="客户同步：定价和交付").title == "客户同步：定价和交付"


def test_the_title_falls_back_to_the_memo_text_then_to_the_meeting_length():
    memo = _titled(mode="memo", memo_text="call the plumber about the leak today", memo_intent="note")
    assert memo.title == "call the plumber about t"
    assert _titled(title=None).title == "会议 42m"
    assert _titled(mode="memo", title=None, memo_text=None).title is None
    assert _titled(title=" \n\t ", dur_ms=70000).title == "会议 1m"


def test_titles_are_cut_to_48_utf8_bytes_on_a_character_boundary_and_lose_control_characters():
    assert _titled(title="题" * 30).title == "题" * 16
    assert _titled(title="ab\ncd\u007f").title == "ab cd"
    assert _titled(mode="memo", memo_text="一二三四五六七八九十" * 3).title == "一二三四五六七八九十一二三四五六"
    assert _titled(title="x" * 47 + "题").title == "x" * 47
    assert _titled(title="bad \ud800 surrogate").title == "bad ? surrogate"


def test_todo_n_defaults_to_the_result_and_is_unknown_until_done():
    assert _titled(title="会", actions_n=3).todo_n == 3
    assert _titled(mode="memo", memo_text="买牛奶", memo_intent="todo").todo_n == 1
    assert _titled(mode="memo", memo_text="想法", memo_intent="note").todo_n == 0
    assert _titled(state="failed", level=None).todo_n is None
    assert page([archived(OLD_1, T0)])[0][0].todo_n is None


def test_a_full_page_fits_the_line_limit():
    sessions_ = [archived(sid(i), T0 + i * HOUR, project="abcdefghijkl", dur_ms=2**32 - 1) for i in range(6)]
    titles = ["录" * 30, '"' * 60, "\\" * 60, "题" * 30, "\u0001" * 60, "é" * 40]
    results = [result(s, title=t, actions_n=20) for s, t in zip(sessions_, titles, strict=True)]
    stars = [r.edited_star(rec=s["rec"], starred=True, at=AT) for s in sessions_]
    events = [*sessions_, *results, *stars, archived(sid(99), T0 - HOUR)]
    items, nxt = page(events)
    line = codec.encode(m.to_wire(rm.RecItems(re="d" + "9" * 23, items=items, next=nxt, id="h" + "9" * 23)))
    assert len(items) == LIMITS["rec_list_max"] and nxt is not None
    assert len(line) <= LIMITS["line_max"] and codec.decode_line(line).ok
    assert all(len(row.title.encode()) <= LIMITS["rec_title_max"] for row in items)


def test_change_appends_only_what_differs_and_knows_no_delete():
    row = {"sid": OLD_1, "starred": False, "project": "cpa", "hidden": False}
    assert history.change(row, "star", None, AT) == (r.edited_star(rec=OLD_1, starred=True, at=AT),)
    assert history.change(row, "unstar", None, AT) == ()
    assert history.change(row, "tag", "cpa", AT) == ()
    assert history.change(row, "tag", None, AT) == (r.edited_project(rec=OLD_1, project=None, at=AT),)
    assert history.change(row, "hide", None, AT) == (r.hidden(rec=OLD_1, at=AT),)
    assert history.change({**row, "hidden": True}, "hide", None, AT) == ()
    for op in ("delete", "trash", "unhide"):
        with pytest.raises(ValueError, match="unknown rec.act op"):
            history.change(row, op, None, AT)


def test_malformed_edit_events_are_skipped():
    events = [archived(OLD_1, T0), {"ev": "rec.edited", "rec": OLD_1, "starred": "yes"},
              {"ev": "rec.edited", "rec": OLD_1, "project": 7}, {"ev": "rec.hidden"}]  # fmt: skip
    assert index.fold(events).edits == {}
    assert page(events)[0][0].starred is None
