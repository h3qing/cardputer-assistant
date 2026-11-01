"""The filing fold (docs/23 §2.1, §2.2, §7.2): one pure fold that every reader asks for the effective project
and the filing state, so cpa rec, the daily note, MCP, todos, captures and the export agree."""

from __future__ import annotations

import json

from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.events.log import EventLog
from cpad.filing import fold as ff
from cpad.projects.registry import Project
from cpad.recordings import index
from cpad.recordings import records as r

DEV = "020000000001"
AT = "2026-10-22T03:23:00.000+00:00"
MEMO, MEETING = "3QZK1M7-0000A-0001", "3QZK1M7-0000A-0002"
KNOWN = ff.Known.of((Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b")))


def archived(rec: str = MEMO, mode: str = "memo", project: str | None = None, dest: str | None = None) -> dict:
    return r.archived(dev=DEV, rec=rec, dir=f"recordings/2026-10-22/1123-{rec.lower()}", mode=mode, project=project,
                      segments=1, dur_ms=24000, started_ms=1792639380000, uncertain=False, gaps=[], consent=None,
                      at=AT, dest=dest)  # fmt: skip


def stored(rec: str, seg: int, project: str | None = None, slot: int | None = None) -> dict:
    return {"ev": "rec.seg.stored", "at": AT, "dev": DEV, "id": f"{rec[:-4]}{seg:04d}", "rec": rec, "seg": seg,
            "file": f"recordings/2026-10-22/1123-{rec.lower()}/seg-{seg:03d}.wav", "mode": "meeting",
            "project": project, "slot": slot}  # fmt: skip


def edited(rec: str = MEMO, **fields) -> dict:
    return {"ev": "rec.edited", "at": AT, "rec": rec, **fields}


def hand(qid: str, project: str = "cpa", rec: str = MEMO, intent: str = "todo") -> dict:
    return {"ev": "queue.added", "at": AT, "qid": qid, "project": project, "text": "x", "intent": intent, "sid": None,
            "origin": "inbox", "from_item": rec, "uncertain": False}  # fmt: skip


def undo(qid: str) -> dict:
    return {"ev": "queue.done", "at": AT, "qid": qid, "by": "undo"}


def reopened(qid: str, project: str = "cpa", intent: str = "todo") -> dict:
    """`cpa queue reopen` (docs/20 U12): the closed item as it was, without from_item."""
    return {"ev": "queue.reopened", "at": AT, "qid": qid, "project": project, "text": "x", "intent": intent,
            "sid": None, "origin": "inbox", "uncertain": False}  # fmt: skip


def state(events, rec: str = MEMO, known: ff.Known = KNOWN) -> ff.Filing:
    events = tuple(events)
    return ff.filing_of(index.fold(events), ff.fold(events), rec, known)


# --- effective project and state ----------------------------------------------------------------


def test_a_memo_without_project_or_destination_is_new_and_one_with_a_registered_project_is_filed():
    assert (state([archived()]).filing, state([archived()]).project) == ("new", None)
    assert (state([archived(project="cpa")]).filing, state([archived(project="cpa")]).project) == ("filed", "cpa")


def test_the_last_project_edit_wins_and_none_unfiles():
    assert state([archived(), edited(project="blog")]).project == "blog"
    found = state([archived(project="cpa"), edited(project="blog"), edited(project=None)])
    assert (found.filing, found.project) == ("new", None)
    assert state([archived(), edited(starred=True)]).project is None, "a star edit sets no project"


def test_a_key_this_mac_does_not_know_is_its_own_group_with_the_key_asked():
    found = state([archived(project="acme")])
    assert (found.filing, found.project, found.orphan, found.asked) == ("new", "acme", True, "acme")
    filed = state([archived(project="acme"), edited(project="cpa")])
    assert (filed.filing, filed.orphan, filed.asked) == ("filed", False, None)


def test_a_meeting_takes_the_last_landed_segment_project_or_slot():
    first_only = [archived(MEETING, "meeting", project="cpa"), stored(MEETING, 0, "cpa", 1), stored(MEETING, 1)]
    assert state(first_only, MEETING).project is None
    by_slot = [archived(MEETING, "meeting"), stored(MEETING, 0), stored(MEETING, 1, slot=2)]
    assert (state(by_slot, MEETING).project, state(by_slot, MEETING).filing) == ("blog", "filed")
    by_key = [archived(MEETING, "meeting"), stored(MEETING, 1, "cpa", 2), stored(MEETING, 0)]
    assert state(by_key, MEETING).project == "cpa", "segment order, not log order"
    assert state([archived(MEETING, "meeting", project="cpa")], MEETING).project == "cpa", "no segments: manifest"


def test_a_hand_is_handed_with_its_project_until_it_is_withdrawn():
    found = state([archived(), hand("q3", "blog")])
    assert (found.filing, found.project, found.hand) == ("handed", "blog", ff.Hand("q3", "todo", "blog"))
    back = state([archived(project="cpa"), edited(project="blog"), hand("q3", "cpa"), undo("q3")])
    assert (back.filing, back.project, back.hand) == ("filed", "blog", None)
    assert state([archived(), hand("q3"), undo("q3")]).filing == "new"


def test_a_hand_that_was_spawned_or_done_stays_handed_and_shows_closed():
    for closing in ({"ev": "queue.spawned", "at": AT, "qid": "q3"}, {"ev": "queue.done", "at": AT, "qid": "q3"}):
        found = state([archived(), hand("q3", intent="spawn"), closing])
        assert (found.filing, found.hand) == ("handed", ff.Hand("q3", "spawn", "cpa", open=False))


def test_a_hand_withdrawn_while_the_device_was_starting_it_stays_handed():
    started = {"ev": "queue.spawned", "at": AT, "qid": "q3"}
    found = state([archived(), hand("q3", intent="spawn"), undo("q3"), started])
    assert (found.filing, found.project, found.hand) == ("handed", "cpa", ff.Hand("q3", "spawn", "cpa", open=False))


def test_a_hand_brought_back_by_cpa_queue_reopen_is_open_again():
    """docs/20 U12: Opt+D on the wrong row, then `cpa queue reopen`: the item is on the board again, so its hand is
    open again and file, hide and a second hand refuse it as they refuse any open hand."""
    for closing in ({"ev": "queue.done", "at": AT, "qid": "q3"}, {"ev": "queue.spawned", "at": AT, "qid": "q3"}):
        found = state([archived(), hand("q3", "blog"), closing, reopened("q3", "blog")])
        assert (found.filing, found.project, found.hand) == ("handed", "blog", ff.Hand("q3", "todo", "blog"))


def test_a_withdrawn_hand_brought_back_by_cpa_queue_reopen_counts_until_withdrawn_again():
    events = [archived(project="cpa"), hand("q3", "blog"), undo("q3"), reopened("q3", "blog")]
    found = state(events)
    assert (found.filing, found.project, found.hand) == ("handed", "blog", ff.Hand("q3", "todo", "blog"))
    again = state([*events, undo("q3")])
    assert (again.filing, again.project, again.hand) == ("filed", "cpa", None)


def test_reopening_a_typed_item_or_a_malformed_reopen_changes_no_filing():
    typed = {**reopened("q7"), "origin": "cli"}
    assert state([archived(), hand("q3"), undo("q3"), typed]).filing == "new"
    torn = {k: v for k, v in reopened("q3").items() if k != "intent"}
    assert state([archived(), hand("q3"), undo("q3"), torn]).filing == "new"


def test_a_project_that_is_not_a_key_is_never_asked_for():
    found = state([archived(project="ACME")])
    assert (found.filing, found.project, found.asked) == ("new", "ACME", None)


def test_done_is_after_filed_and_reopen_clears_it():
    assert state([archived(), edited(done=True)]).filing == "done"
    assert state([archived(project="cpa"), edited(done=True)]).filing == "filed"
    assert state([archived(), edited(done=True), edited(done=False)]).filing == "new"
    assert state([archived(project="acme"), edited(done=True)]).asked is None, "only new rows are orphans"


def test_hidden_wins_over_everything():
    for extra in ([], [hand("q1")], [edited(done=True)], [edited(project="cpa")]):
        assert state([archived(), *extra, {"ev": "rec.hidden", "at": AT, "rec": MEMO}]).filing == "hidden"
    assert state([archived(), {"ev": "rec.hidden", "rec": MEMO}, {"ev": "rec.unhidden", "rec": MEMO}]).filing == "new"


def test_only_a_todo_destination_counts_before_docs14():
    found = state([archived(dest="todo")])
    assert (found.filing, found.dest) == ("filed", "todo")
    downgraded = {"ev": "dest.downgraded", "at": AT, "item": MEMO, "dest": "todo"}
    assert state([archived(dest="todo"), downgraded]).filing == "new"
    for dest in ("daily", "queue", "session", "file.notes"):
        assert state([archived(dest=dest)]).filing == "new", dest


def test_malformed_records_are_skipped():
    bad = [edited(done="yes"), edited(project=3), hand("q1", rec=None), {"ev": "queue.added", "qid": "q2",
           "from_item": MEMO}, {"ev": "queue.done", "by": "undo"}, {"ev": "rec.result", "rec": 7}]  # fmt: skip
    assert state([archived(), *bad]) == state([archived()])


def test_a_duplicate_queue_item_is_ignored():
    assert state([archived(), hand("q1", "cpa"), edited(project="blog"), hand("q1", "cpa")]).project == "blog"


def test_projects_cover_every_session_the_log_knows_including_unarchived_ones():
    events = (archived(project="cpa"), stored(MEETING, 0, "blog", 2), edited(project="blog"))
    found = ff.projects(index.fold(events), ff.fold(events), KNOWN)
    assert dict(found) == {MEMO: "blog", MEETING: "blog"}


def test_filings_list_archived_sessions_only():
    events = (archived(project="cpa"), stored(MEETING, 0, "blog", 2))
    assert [f.rec for f in ff.filings(index.fold(events), ff.fold(events), KNOWN)] == [MEMO]


def test_known_of_an_empty_registry_knows_nothing():
    assert state([archived(project="cpa")], known=ff.Known()).orphan is True


# --- properties (docs/23 §7.2) ------------------------------------------------------------------

ACTIONS = st.lists(
    st.one_of(
        st.builds(lambda p: edited(project=p), st.sampled_from(["cpa", "blog", "acme", None])),
        st.builds(lambda d: edited(done=d), st.booleans()),
        st.builds(lambda n, p: hand(f"q{n}", p), st.integers(1, 4), st.sampled_from(["cpa", "blog"])),
        st.builds(lambda n: undo(f"q{n}"), st.integers(1, 4)),
        st.builds(lambda n: {"ev": "queue.spawned", "at": AT, "qid": f"q{n}"}, st.integers(1, 4)),
        st.builds(lambda n: reopened(f"q{n}"), st.integers(1, 4)),
        st.sampled_from([{"ev": "rec.hidden", "at": AT, "rec": MEMO}, {"ev": "rec.unhidden", "at": AT, "rec": MEMO}]),
    ),
    max_size=12,
)


@settings(max_examples=150, deadline=None)
@given(ACTIONS, st.integers(0, 12), st.sampled_from(["cpa", "acme", None]))
def test_folding_in_any_chunks_gives_the_same_state(actions, cut, recorded):
    events = (archived(project=recorded), *actions)
    whole = ff.fold(events)
    chunked = ff.fold(events[cut:], ff.fold(events[:cut]))
    assert chunked == whole
    assert ff.filing_of(index.fold(events), chunked, MEMO, KNOWN) == state(events)


def _inverse(before: ff.Filing, action: str, events: tuple) -> tuple[dict, dict]:
    """(the action, the record its undo writes: docs/23 §3.3 rule 6)."""
    qid = f"q{100 + len(events)}"
    return {
        "file": (edited(project="blog"), edited(project=before.project)),
        "unfile": (edited(project=None), edited(project=before.project)),
        "done": (edited(done=True), edited(done=False)),
        "hand": (hand(qid, "cpa"), undo(qid)),
        "hide": ({"ev": "rec.hidden", "at": AT, "rec": MEMO}, {"ev": "rec.unhidden", "at": AT, "rec": MEMO}),
    }[action]


@settings(max_examples=150, deadline=None)
@given(ACTIONS, st.sampled_from(["file", "unfile", "done", "hand", "hide"]), st.sampled_from(["cpa", "acme", None]))
def test_an_action_then_its_undo_returns_to_the_same_state(actions, action, recorded):
    events = (archived(project=recorded), *actions)
    before = state(events)
    if (action == "done" and before.done) or (action == "hide" and before.filing == "hidden"):
        return  # the CLI answers changed:false there and writes nothing
    done, undone = _inverse(before, action, events)
    after = state((*events, done, undone))
    assert (after.filing, after.project, after.asked, after.hand) == (
        before.filing, before.project, before.asked, before.hand)  # fmt: skip


def test_a_torn_last_line_is_an_action_not_done_and_redoing_it_converges(tmp_path):
    log = EventLog(tmp_path / "events.jsonl")
    log.append((archived(),))
    action = edited(project="cpa", by="cli")
    with open(tmp_path / "events.jsonl", "ab") as fh:
        fh.write(json.dumps(action).encode()[:-9])  # a crash in the middle of the one line
    assert state(log.load()).filing == "new"
    log.append((action,))  # the same action again
    assert state(EventLog(tmp_path / "events.jsonl").load()).filing == "filed"


# --- the daily note: rec.filed (docs/23 §2.4) ---------------------------------------------------


def result(rec: str = MEMO, project: str | None = None, title: str | None = "报价") -> dict:
    return r.result(rec=rec, state="done", level="summary", title=title, memo_text="周五前发报价", memo_intent="note",
                    actions_n=0, error_code=None, session=archived(rec, project=project), at=AT)  # fmt: skip


def pending(events) -> tuple[dict, ...]:
    events = tuple(events)
    return ff.pending_filed(index.fold(events), ff.fold(events), KNOWN, AT)


def test_filing_after_the_result_asks_for_one_self_contained_rec_filed():
    events = (archived(), result(), edited(project="cpa", by="cli"))
    (found,) = pending(events)
    assert found == {"ev": "rec.filed", "at": AT, "rec": MEMO, "project": "cpa",
                     "dir": f"recordings/2026-10-22/1123-{MEMO.lower()}", "mode": "memo", "started_ms": 1792639380000,
                     "uncertain": False, "title": "报价", "n": 1}  # fmt: skip
    assert pending((*events, found)) == (), "once written, the same state asks for nothing"
    assert pending((*events, found, edited(project="blog"), edited(project="cpa"))) == ()


def test_filing_back_to_an_earlier_project_asks_for_a_new_line_each_time():
    events = (archived(), result(), edited(project="cpa"))
    (first,) = pending(events)
    events = (*events, first, edited(project="blog"))
    (second,) = pending(events)
    events = (*events, second, edited(project="cpa"))
    (third,) = pending(events)
    assert [(f["project"], f["n"]) for f in (first, second, third)] == [("cpa", 1), ("blog", 2), ("cpa", 3)]
    assert pending((*events, third)) == ()


def test_a_hand_after_the_result_is_filed_to_its_project():
    (found,) = pending((archived(), result(title=None), hand("q1", "blog")))
    assert (found["project"], found["title"]) == ("blog", None)


def test_a_stale_line_counts_as_a_line():
    stale = r.stale(rec=MEMO, session=archived(), at=AT)
    assert [f["project"] for f in pending((archived(), stale, edited(project="cpa")))] == ["cpa"]


def test_nothing_to_write_when_the_line_already_shows_it_or_nothing_was_filed_here():
    assert pending((archived(), edited(project="cpa"), result(project="cpa"))) == (), "filed before the result"
    assert pending((archived(), edited(project="cpa"))) == (), "no line yet"
    assert pending((archived(), result(), edited(project="acme"))) == (), "not this Mac's project"
    assert pending((archived(project="cpa"), result(project="cpa"), edited(project=None))) == (), "unfiled"
    assert pending((archived(), result(), edited(project="cpa"), {"ev": "rec.hidden", "rec": MEMO})) == ()
    assert pending((archived(), result(), edited(done=True))) == (), "done writes no note"
