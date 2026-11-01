"""Pure folds and renderers: queue (05 §7.7), event records (§7.3), daily notes (§7.8); v0.4.1 todo notes."""

from __future__ import annotations

import time
from datetime import UTC, datetime, timedelta, timezone

import pytest

from cpad.events import records
from cpad.notes import render
from cpad.protocol import messages as m
from cpad.queue import fold

AT = "2026-10-19T17:00:00.000+00:00"


def _added(qid, project="cpa", text="t", intent="todo", **kw):
    return records.queue_added(qid=qid, project=project, text=text, intent=intent, origin="cli", at=AT, **kw)


# --- queue fold -----------------------------------------------------------------------------


def test_fold_adds_and_removes_items():
    events = [
        _added("q1"),
        _added("q2", intent="spawn"),
        records.queue_done("q1", at=AT),
        _added("q3"),
        records.queue_spawned("q2", at=AT),
    ]
    state = fold.fold_queue(events)
    assert [i.qid for i in state.items] == ["q3"]
    assert state.last_qid == 3 and fold.next_qid(state) == "q4"


def test_fold_is_incremental_and_ignores_noise():
    first = fold.fold_queue([_added("q7")])
    later = fold.fold_queue([{"ev": "queue.added"}, {"ev": "item.received"}, _added("q7"), _added("q8")], first)
    assert [i.qid for i in later.items] == ["q7", "q8"]


def _reopened(events, qid):
    item = fold.closed_item(events, qid)
    assert item is not None
    return records.queue_reopened(item, at=AT)


def test_reopen_brings_a_closed_item_back_as_it_was():
    """docs/20 U12: an item marked done by mistake (or spawned) comes back with its qid, text and flags."""
    events = [
        _added("q1", text="ask lawyer"),
        _added("q2", intent="spawn", uncertain=True),
        records.queue_done("q1", at=AT),
        records.queue_spawned("q2", at=AT),
    ]
    assert fold.closed_item(events, "q1").text == "ask lawyer"
    events += [_reopened(events, "q1"), _reopened(events, "q2")]
    state = fold.fold_queue(events)
    assert [(i.qid, i.text) for i in fold.open_items(state)] == [("q2", "t"), ("q1", "ask lawyer")]
    assert fold.find(state, "q2").uncertain is True
    assert fold.closed_item(events, "q1") is None  # open again
    again = [*events, records.queue_done("q1", at=AT)]
    assert fold.find(fold.fold_queue(again), "q1") is None and fold.closed_item(again, "q1").text == "ask lawyer"


def test_reopen_of_an_open_or_unknown_item_changes_nothing():
    events = [_added("q1")]
    assert fold.closed_item(events, "q1") is None and fold.closed_item(events, "q9") is None
    state = fold.fold_queue(events)
    stray = records.queue_reopened(fold.find(state, "q1"), at=AT)
    assert fold.fold_queue([stray], state) == state


def test_fold_does_not_carry_the_closed_items():
    """Review: the fold kept every closed item and rebuilt that tuple on each close (20k items: 4.5 s, at every
    `cpa` call and MCP queue call). The reopened record carries the item instead, so the fold stays linear."""
    events = [e for n in range(1, 20001) for e in (_added(f"q{n}"), records.queue_done(f"q{n}", at=AT))]
    start = time.perf_counter()
    state = fold.fold_queue(events)
    assert time.perf_counter() - start < 1.0
    assert state.items == () and state.last_qid == 20000


def test_assign_qids_fills_missing_ids_in_order():
    state = fold.fold_queue([_added("q4")])
    pending = ({"ev": "item.received", "id": "x"}, _added(None), _added(None))
    out = fold.assign_qids(pending, state)
    assert [r.get("qid") for r in out] == [None, "q5", "q6"]
    assert pending[1]["qid"] is None  # inputs are not mutated


def test_open_items_newest_first_and_by_project():
    state = fold.fold_queue([_added("q1", project="a"), _added("q2", project="b"), _added("q3", project="a")])
    assert [i.qid for i in fold.open_items(state)] == ["q3", "q2", "q1"]
    assert [i.qid for i in fold.open_items(state, "a")] == ["q3", "q1"]
    assert fold.find(state, "q2").project == "b" and fold.find(state, "q9") is None


# --- event records --------------------------------------------------------------------------


def test_item_received_record_keeps_device_time_and_orphan_flag():
    item = m.OutboxItem(id="3QZK1M7-0000C-0003", kind="note", at=m.At(41000, None), body=m.NoteBody("idea", "old"))
    rec = records.item_received("020000000001", item, received_at=AT, orphan=True)
    assert rec == {
        "ev": "item.received", "at": AT, "dev": "020000000001", "id": "3QZK1M7-0000C-0003", "kind": "note",
        "body": {"text": "idea", "project": "old"}, "at_dev": {"up_ms": 41000, "wall_ms": None},
        "received_at": AT, "orphan": True,
    }  # fmt: skip


def test_queue_records_from_a_device_item():
    item = m.OutboxItem(
        id="3QZK1M7-0000C-0004", kind="queue", at=m.At(1), body=m.QueueBody("go", "cpa", "spawn", None, True)
    )
    rec = records.queue_added_from_item(item, at=AT)
    assert rec["origin"] == "device" and rec["from_item"] == item.id and rec["uncertain"] is True
    assert rec["qid"] is None and rec["intent"] == "spawn"


def test_agent_records_never_carry_text():
    for rec in (
        records.agent_spawned("cpa", "cpa/x", "a1b2c3d4", at=AT),
        records.agent_spawn_failed("cpa", "cpa/x", "E_SPAWN", at=AT),
        records.agent_replied("5c2e8a14", at=AT),
        records.agent_reply_failed("5c2e8a14", "E_REPLY_FORKED", at=AT),
    ):
        assert "text" not in rec and "body" not in rec


def test_a_note_dest_is_recorded_only_when_the_device_sent_one():
    plain = m.OutboxItem(id="3QZK1M7-0000C-0005", kind="note", at=m.At(1), body=m.NoteBody("x", "cpa"))
    todo = m.OutboxItem(id="3QZK1M7-0000C-0005", kind="note", at=m.At(1), body=m.NoteBody("x", "cpa", "todo"))
    assert records.item_received("dev", plain, AT, False)["body"] == {"text": "x", "project": "cpa"}
    assert records.item_received("dev", todo, AT, False)["body"] == {"text": "x", "project": "cpa", "dest": "todo"}


def test_dest_downgraded_names_the_item_and_dest_but_never_the_text():
    rec = records.dest_downgraded("3QZK1M7-0000C-0005", "ww.plugin.todoist", at=AT)
    assert rec == {"ev": "dest.downgraded", "at": AT, "item": "3QZK1M7-0000C-0005", "dest": "ww.plugin.todoist"}


def test_dedupe_keys_come_from_item_received():
    item = m.OutboxItem(id="3QZK1M7-0000C-0003", kind="note", at=m.At(1), body=m.NoteBody("x"))
    events = [records.item_received("dev1", item, AT, False), _added("q1"), {"ev": "item.received", "id": 3}]
    assert records.dedupe_keys(events) == frozenset({("dev1", "3QZK1M7-0000C-0003")})


def test_iso_time_is_utc_with_milliseconds():
    assert records.iso(0.0) == "1970-01-01T00:00:00.000+00:00"


# --- notes ----------------------------------------------------------------------------------

TZ = timezone(timedelta(hours=-7))


def _note(text="idea: dedupe by sid", project="cpa", wall_ms=1792440000000, orphan=False, received_at=AT, dest=None):
    body = m.NoteBody(text, project, dest)
    item = m.OutboxItem(id="3QZK1M7-0000C-0003", kind="note", at=m.At(41000, wall_ms), body=body)
    return records.item_received("dev", item, received_at=received_at, orphan=orphan)


def test_note_line_uses_device_wall_clock():
    name, line = render.note_entry(_note(), TZ)
    wall = datetime.fromtimestamp(1792440000, TZ)
    assert name == wall.strftime("%Y-%m-%d") + ".md"
    assert line == f"- {wall:%H:%M} #cpa idea: dedupe by sid ^cpa-3qzk1m7-0000c-0003"


def test_note_without_wall_clock_uses_received_time_with_tilde():
    _, line = render.note_entry(_note(wall_ms=None, received_at="2026-10-19T17:05:00.000+00:00"), UTC)
    assert line.startswith("- ~17:05 #cpa ")


@pytest.mark.parametrize("wall_ms", [253402300800000, 9007199254740991], ids=["year 10000", "2^53-1"])
def test_out_of_range_wall_clock_falls_back_to_received_time(wall_ms):
    _, line = render.note_entry(_note(wall_ms=wall_ms, received_at="2026-10-19T17:05:00.000+00:00"), UTC)
    assert line.startswith("- ~17:05 #cpa ")


def test_unassigned_and_orphan_notes_have_no_tag():
    assert " #" not in render.note_entry(_note(project=None), TZ)[1]
    assert " #" not in render.note_entry(_note(orphan=True), TZ)[1]


def test_control_characters_become_spaces():
    assert "a b" in render.note_entry(_note(text="a\tb"), TZ)[1]


def test_only_note_items_render():
    assert render.note_entry(_added("q1"), TZ) is None
    queue = _note()
    assert render.note_entry({**queue, "kind": "queue"}, TZ) is None


def test_block_id_is_lower_case():
    assert render.block_id("3QZK1M7-0000C-0003") == "^cpa-3qzk1m7-0000c-0003"


# --- v0.4.1 todo notes (docs/13 §9.3, §11.1) ---------------------------------------------------


def test_a_todo_note_is_a_checkbox_line_like_a_recording_todo():
    _, line = render.note_entry(_note(text="call the plumber", dest="todo"), TZ)
    wall = datetime.fromtimestamp(1792440000, TZ)
    assert line == f"- [ ] {wall:%H:%M} #cpa call the plumber ^cpa-3qzk1m7-0000c-0003"


def test_a_todo_note_without_wall_clock_keeps_the_tilde_after_the_box():
    _, line = render.note_entry(_note(dest="todo", wall_ms=None, received_at="2026-10-19T17:05:00.000+00:00"), UTC)
    assert line.startswith("- [ ] ~17:05 #cpa ")


def test_an_unassigned_todo_note_has_no_tag():
    assert render.note_entry(_note(project=None, dest="todo"), TZ)[1].startswith("- [ ] ")
    assert " #" not in render.note_entry(_note(project=None, dest="todo"), TZ)[1]


@pytest.mark.parametrize("dest", ["daily", "clip", "ww.plugin.todoist"])
def test_any_other_dest_is_the_v03_line_byte_for_byte(dest):
    assert render.note_entry(_note(dest=dest), TZ) == render.note_entry(_note(), TZ)


def test_known_dests_are_the_daily_note_and_todo():
    assert frozenset({"daily", "todo"}) == render.KNOWN_DESTS
