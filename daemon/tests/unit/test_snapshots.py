"""Snapshot building (05 §7.6): caps, ordering, UTF-8 truncation, 2048 B fit, rev only on content change."""

from __future__ import annotations

from dataclasses import replace

import pytest

from cpad.agents.base import SessionInfo
from cpad.events import records
from cpad.hub import snapshots
from cpad.hub.state import HubState
from cpad.inbox.need import Tracked
from cpad.projects.registry import Project
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.queue.fold import fold_queue

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

AT = "2026-10-19T17:00:00.000+00:00"
CPA = Project("cpa", "Cardputer 助手", 1, "/r/cpa")
BLOG = Project("blog", "博客", 2, "/r/blog", ok=False)


def _t(sid, status="busy", project="cpa", since=0.0, kind="bg", name=None):
    info = SessionInfo(
        f"{sid}-0000", sid, sid if kind == "bg" else None, kind, status, None, name or f"会话{sid}", "/r"
    )
    return Tracked(info, project, since)


def _state(tracked=(), queue_events=(), **kw):
    return HubState(projects=(CPA, BLOG), tracked=tuple(tracked), queue=fold_queue(queue_events), **kw)


def _q(qid, project="cpa", text="todo item", intent="todo"):
    return records.queue_added(qid=qid, project=project, text=text, intent=intent, origin="cli", at=AT)


def _valid(msg) -> bool:
    return codec.decode_line(codec.encode(m.to_wire(replace(msg, id="h4294967295")))).ok


def test_board_counts_run_need_queued_and_other():
    tracked = [_t("aaaaaaa1"), _t("aaaaaaa2", "waiting"), _t("aaaaaaa3", "shell", None), _t("aaaaaaa4", "idle", None)]
    _, snaps, _ = snapshots.publish(_state(tracked, [_q("q1"), _q("q2"), _q("q3", "blog")]), now=10.0)
    rows = {r.key: r for r in snaps.projects.projects}
    assert (rows["cpa"].run, rows["cpa"].need, rows["cpa"].queued, rows["cpa"].ok) == (1, 1, 2, True)
    assert (rows["blog"].queued, rows["blog"].ok) == (1, False)
    assert snaps.projects.other == m.Other(run=1, n=2)
    assert _valid(snaps.projects)


def test_sessions_need_first_then_busy_then_rest_and_capped_at_ten():
    tracked = [_t(f"0000000{i}", "idle", since=float(i)) for i in range(8)]
    tracked += [_t("bbbbbbb1", "waiting", since=5.0), _t("bbbbbbb2", "waiting", since=1.0), _t("ccccccc1", "busy")]
    tracked += [_t("ddddddd1", "idle", project=None)]
    _, snaps, _ = snapshots.publish(_state(tracked), now=30.0)
    sids = [r.sid for r in snaps.sessions.sessions]
    assert sids[:3] == ["bbbbbbb2", "bbbbbbb1", "ccccccc1"]
    assert len(sids) == 10 and snaps.sessions.total == 11
    assert "ddddddd1" not in sids
    assert _valid(snaps.sessions)


def test_session_rows_fields():
    _, snaps, _ = snapshots.publish(_state([_t("aaaaaaa1", "idle", since=0.0), _t("bbbbbbb1", kind="tty")]), now=90.0)
    rows = {r.sid: r for r in snaps.sessions.sessions}
    assert rows["aaaaaaa1"] == m.SessionRow("aaaaaaa1", 1, "会话aaaaaaa1", "bg", "idle", True, 90, "aaaaaaa1")
    assert rows["bbbbbbb1"].short is None and rows["bbbbbbb1"].kind == "tty"


def test_a_repeated_wire_sid_is_sent_once_so_the_device_keeps_the_snapshot():
    """Two listings whose session ids share their first 8 hex digits (the wire sid): the device refuses a
    snapshot with a repeated sid (E_BAD_FIELD sessions[i].sid), so the first in display order is kept."""
    tracked = [_t("aaaaaaa2", "busy"), _t("aaaaaaa1", "waiting"), _t("aaaaaaa2", "idle", kind="tty", name="tty")]
    _, snaps, _ = snapshots.publish(_state(tracked), now=1.0)
    rows = snaps.sessions.sessions
    assert [(r.sid, r.kind) for r in rows] == [("aaaaaaa1", "bg"), ("aaaaaaa2", "bg")]
    assert snaps.sessions.total == 2
    assert _valid(snaps.sessions)


def test_a_dropped_repeat_is_not_counted_on_the_board_either():
    """The board counts the rows the sessions snapshot lists, so a project never shows a run or need
    that no row on its page explains."""
    tracked = [_t("aaaaaaa1", "waiting"), _t("aaaaaaa1", "busy", kind="tty", name="tty")]
    _, snaps, _ = snapshots.publish(_state(tracked), now=1.0)
    cpa = {r.key: r for r in snaps.projects.projects}["cpa"]
    assert [(r.sid, r.st) for r in snaps.sessions.sessions] == [("aaaaaaa1", "waiting")]
    assert (cpa.run, cpa.need) == (0, 1)


def test_long_cjk_names_are_cut_on_character_boundaries_and_fit_2048():
    tracked = [_t(f"{i:08x}", name="设置页面和字体修复" * 5 + "\n") for i in range(10)]
    project = Project("cpa", "名" * 8, 1, "/r/cpa")
    state = replace(_state(tracked), projects=(project,))
    _, snaps, _ = snapshots.publish(state, now=1.0)
    for row in snaps.sessions.sessions:
        assert len(row.name.encode()) <= 32 and "\n" not in row.name
        row.name.encode().decode()
    assert snaps.projects.projects[0].name == "名" * 8
    assert len(codec.encode(m.to_wire(replace(snaps.sessions, id="h" + "9" * 23)))) <= 2048
    assert _valid(snaps.sessions)


def test_a_name_that_starts_with_its_own_key_is_sent_without_it():
    """docs/20 U14: the device writes key/name, so cpa/fix... would show as cpa/cpa/fix...; the 32 bytes
    now carry the name itself, in full."""
    tracked = [
        _t("aaaaaaa1", name="cpa/fix-flaky-refund-webhook-timeout"),
        _t("aaaaaaa2", name="blog/draft"),
        _t("aaaaaaa3", name="cpa/"),
        _t("aaaaaaa4", name="cpa-old/x"),
    ]
    _, snaps, _ = snapshots.publish(_state(tracked), now=1.0)
    names = {r.sid: r.name for r in snaps.sessions.sessions}
    assert names["aaaaaaa1"] == "fix-flaky-refund-webhook-timeout"
    assert (names["aaaaaaa2"], names["aaaaaaa3"], names["aaaaaaa4"]) == ("blog/draft", "cpa/", "cpa-old/x")
    assert _valid(snaps.sessions)


def test_sessions_snapshot_drops_tail_entries_to_fit_2048():
    names = ["\\" * 32] * 10
    tracked = [_t(f"{i:08x}", name=n) for i, n in enumerate(names)]
    _, snaps, _ = snapshots.publish(_state(tracked), now=1.0)
    assert 0 < len(snaps.sessions.sessions) <= 10
    assert len(codec.serialize(m.to_wire(replace(snaps.sessions, id="h" + "9" * 23)))) <= 2048
    assert snaps.sessions.total == 10


def test_queue_newest_first_four_per_project_twelve_total():
    events = [_q(f"q{i}", "cpa" if i % 2 else "blog", text="长" * 40) for i in range(1, 21)]
    events += [_q("q99", "gone")]
    _, snaps, _ = snapshots.publish(_state(queue_events=events), now=1.0)
    items = snaps.queue.items
    assert [i.qid for i in items if i.slot == 1] == ["q19", "q17", "q15", "q13"]
    assert len(items) == 8 and all(len(i.text.encode()) <= 80 for i in items)
    assert "gone" not in {i.slot for i in items}
    assert _valid(snaps.queue)


def test_an_item_handed_from_a_recording_reads_voice_on_the_board():
    """docs/23 §3.4, docs/22 §3.8: the summary of an inbox item starts with 语音· (Voice·), still at most 80 B."""
    handed = records.queue_handed(qid="q2", project="cpa", text="长" * 40, intent="todo", rec="3QZK1M7-0000A-0001",
                                  at=AT)  # fmt: skip
    _, snaps, _ = snapshots.publish(_state(queue_events=[_q("q1"), handed]), now=1.0)
    voice, typed = snaps.queue.items
    assert voice.text == "语音·" + "长" * 24 and len(voice.text.encode()) == 80
    assert typed.text == "todo item" and _valid(snaps.queue)


def test_an_uncertain_spawn_item_says_so_and_others_are_unchanged():
    """docs/20 U25: an Opt+S whose result never came back is marked; a plain row keeps its v0.4.0 bytes."""
    events = [_q("q1", intent="spawn"), {**_q("q2", intent="spawn"), "uncertain": True}]
    _, snaps, _ = snapshots.publish(_state(queue_events=events), now=1.0)
    rows = {i.qid: i for i in snaps.queue.items}
    assert rows["q2"].uncertain is True and rows["q1"].uncertain is None
    wire = {i["qid"]: i for i in m.to_wire(snaps.queue)["items"]}
    assert wire["q2"]["uncertain"] is True and "uncertain" not in wire["q1"]
    assert _valid(snaps.queue)


def test_rev_changes_only_with_content_not_age():
    state = _state([_t("aaaaaaa1", "busy")])
    published, snaps, changed = snapshots.publish(state, now=1.0)
    assert snaps.sessions.rev == 1 and set(changed) == {"projects", "sessions", "queue"}
    state = replace(state, published=published)
    published2, snaps2, changed2 = snapshots.publish(state, now=500.0)
    assert changed2 == () and snaps2.sessions.rev == 1 and snaps2.sessions.sessions[0].age_s == 500
    state = replace(state, published=published2, tracked=(_t("aaaaaaa1", "idle", since=500.0),))
    _, snaps3, changed3 = snapshots.publish(state, now=501.0)
    assert changed3 == ("projects", "sessions") and snaps3.sessions.rev == 2


def test_behind_lists_kinds_whose_revs_differ():
    assert snapshots.behind(m.Revs(42, 18, 10), m.Revs(42, 17, 10)) == ("sessions",)
    assert snapshots.behind(m.Revs(1, 1, 1), m.Revs(1, 1, 1)) == ()


def test_truncate_utf8():
    assert snapshots.truncate_utf8("ab会", 4) == "ab"
    assert snapshots.label("\t\n", 24, "fallback") == "fallback"
