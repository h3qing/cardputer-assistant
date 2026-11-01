"""The writes of `cpa filing` (docs/23 §2.2, §3.3): file, done, hide, reopen. Each is one record or none, decided
from the fold; already in that state writes nothing (changed false); every change comes with its undo. Pure."""

from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.filing import actions
from cpad.filing import fold as ff
from cpad.filing.args import FilingError
from cpad.projects.registry import Project
from cpad.recordings import index
from cpad.recordings import records as r

DEV = "020000000001"
AT = "2026-10-22T03:23:00.000+00:00"
MEMO = "3QZK1M7-0000A-0001"
KNOWN = ff.Known.of((Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b")))


def archived(project: str | None = None) -> dict:
    return r.archived(dev=DEV, rec=MEMO, dir=f"recordings/2026-10-22/1123-{MEMO.lower()}", mode="memo",
                      project=project, segments=1, dur_ms=24000, started_ms=1792639380000, uncertain=False, gaps=[],
                      consent=None, at=AT)  # fmt: skip


def hand(qid: str = "q3") -> dict:
    return {"ev": "queue.added", "at": AT, "qid": qid, "project": "cpa", "text": "x", "intent": "spawn", "sid": None,
            "origin": "inbox", "from_item": MEMO, "uncertain": False}  # fmt: skip


def plan(op: str, events, **ask) -> actions.Plan:
    return actions.plan(actions.Ask(op, ask.pop("rec", MEMO.lower()), **ask), tuple(events), KNOWN, AT)


def after(events, p: actions.Plan) -> ff.Filing:
    found = (*events, *p.records)
    return ff.filing_of(index.fold(found), ff.fold(found), MEMO, KNOWN)


def refused(op: str, events, **ask) -> tuple[str, str | None]:
    with pytest.raises(FilingError) as err:
        plan(op, events, **ask)
    return err.value.code, err.value.field


# --- file ---------------------------------------------------------------------------------------


def test_file_writes_one_edit_by_the_cli_and_undoes_to_the_project_before():
    events = [archived()]
    p = plan("file", events, project="cpa")
    assert p.records == ({"ev": "rec.edited", "at": AT, "rec": MEMO, "project": "cpa", "by": "cli"},)
    assert p.undo == {"op": "file", "id": MEMO, "args": {"project": None}}
    assert (after(events, p).filing, after(events, p).project) == ("filed", "cpa")
    none = plan("file", [*events, *p.records], none=True)
    assert none.records[0]["project"] is None and none.undo["args"] == {"project": "cpa"}


def test_file_is_refused_under_an_open_queue_item_which_keeps_its_project():
    events = [archived(), hand()]
    assert refused("file", events, project="blog") == ("E_ALREADY", "queue")
    assert refused("file", events, none=True) == ("E_ALREADY", "queue")
    assert plan("file", events, project="cpa").records == (), "its own project is still a no-op"
    spawned = [*events, {"ev": "queue.spawned", "at": AT, "qid": "q3"}]
    assert plan("file", spawned, project="blog").records[0]["project"] == "blog"


def test_the_undo_of_file_restores_a_key_this_mac_no_longer_has():
    events = [archived(), {"ev": "rec.edited", "at": AT, "rec": MEMO, "project": "old"}]  # tagged, then removed here
    p = plan("file", events, project="cpa", other=True)
    assert p.undo["args"] == {"project": "old"}
    back = plan("file", [*events, *p.records], project="old")
    assert (after([*events, *p.records], back).project, after([*events, *p.records], back).asked) == ("old", "old")
    assert refused("file", [archived()], project="old") == ("E_UNKNOWN_PROJECT", "project"), "only its own keys"


def test_a_project_that_is_not_a_key_is_not_asked_for_and_its_undo_is_no_project():
    events = [archived("ACME")]
    p = plan("file", events, project="cpa")
    assert p.undo["args"] == {"project": None}


def test_filing_to_the_project_it_already_has_writes_nothing():
    assert plan("file", [archived("cpa")], project="cpa") == actions.Plan(MEMO, (), None)
    assert plan("file", [archived()], none=True).records == ()


@pytest.mark.parametrize(
    ("ask", "want"),
    [
        ({"project": "acme"}, ("E_UNKNOWN_PROJECT", "project")),
        ({"project": "Cpa"}, ("E_BAD_ARG", "project")),
        ({}, ("E_BAD_ARG", "project")),
        ({"project": "cpa", "none": True}, ("E_BAD_ARG", "project")),
        ({"project": "cpa", "rec": "nope"}, ("E_BAD_ARG", "id")),
        ({"project": "cpa", "rec": "3QZK1M7-0000A-0009"}, ("E_UNKNOWN_ID", "id")),
    ],
)
def test_file_refusals(ask, want):
    assert refused("file", [archived()], **ask) == want


def test_a_session_still_landing_is_not_in_the_list_yet():
    landing = {"ev": "rec.seg.stored", "at": AT, "dev": DEV, "id": MEMO, "rec": MEMO, "seg": 0, "mode": "memo",
               "file": f"recordings/2026-10-22/1123-{MEMO.lower()}/seg-000.wav"}  # fmt: skip
    assert refused("done", [landing]) == ("E_UNKNOWN_ID", "id")


def test_the_other_mac_group_needs_other_and_its_undo_restores_the_key_it_asked_for():
    events = [archived("acme")]
    assert refused("file", events, project="cpa") == ("E_BAD_ARG", "other")
    p = plan("file", events, project="cpa", other=True)
    assert p.undo == {"op": "file", "id": MEMO, "args": {"project": "acme"}}
    back = plan("file", [*events, *p.records], project="acme")
    restored = after([*events, *p.records], back)
    assert (restored.filing, restored.asked) == ("new", "acme")
    assert refused("file", [archived()], project="acme") == ("E_UNKNOWN_PROJECT", "project"), "only its own key"
    assert plan("file", events, project="acme").records == (), "a no-op needs no confirmation"


# --- done, hide, reopen -------------------------------------------------------------------------


def test_done_and_its_undo():
    p = plan("done", [archived()])
    assert p.records == ({"ev": "rec.edited", "at": AT, "rec": MEMO, "done": True, "by": "cli"},)
    assert p.undo == {"op": "reopen", "id": MEMO, "args": {}}
    assert after([archived()], p).filing == "done"
    assert plan("done", [archived(), *p.records]).records == ()


@pytest.mark.parametrize("extra", [[], [hand()], [{"ev": "rec.hidden", "rec": MEMO}]], ids=["filed", "hand", "hid"])
def test_done_is_only_for_a_new_recording(extra):
    start = archived("cpa") if not extra else archived()
    assert refused("done", [start, *extra]) == ("E_ALREADY", "filing")


def test_hide_and_its_undo_and_no_hiding_under_an_open_queue_item():
    p = plan("hide", [archived()])
    assert p.records == ({"ev": "rec.hidden", "at": AT, "rec": MEMO, "by": "cli"},)
    assert p.undo == {"op": "reopen", "id": MEMO, "args": {}}
    assert plan("hide", [archived(), *p.records]).records == ()
    assert refused("hide", [archived(), hand()]) == ("E_ALREADY", "queue")
    spawned = [archived(), hand(), {"ev": "queue.spawned", "at": AT, "qid": "q3"}]
    assert plan("hide", spawned).records[0]["ev"] == "rec.hidden"


@pytest.mark.parametrize(
    ("events", "record", "undo"),
    [
        ([archived(), {"ev": "rec.hidden", "rec": MEMO}], {"ev": "rec.unhidden", "rec": MEMO, "by": "cli"}, "hide"),
        (
            [archived(), {"ev": "rec.edited", "rec": MEMO, "done": True}],
            {"ev": "rec.edited", "rec": MEMO, "done": False, "by": "cli"},
            "done",
        ),
        ([archived(), hand()], {"ev": "queue.done", "qid": "q3", "by": "undo"}, None),
    ],
    ids=["hidden", "done", "handed"],
)
def test_reopen_takes_back_the_one_reason(events, record, undo):
    p = plan("reopen", events)
    assert p.records == ({**record, "at": AT},)
    assert p.undo == (None if undo is None else {"op": undo, "id": MEMO, "args": {}})
    assert after(events, p).filing == "new"


def test_unhiding_under_an_open_queue_item_offers_no_undo_that_hide_would_refuse():
    p = plan("reopen", [archived(), hand(), {"ev": "rec.hidden", "rec": MEMO}])
    assert p.records[0]["ev"] == "rec.unhidden" and p.undo is None


def test_reopen_refuses_what_it_cannot_take_back():
    assert plan("reopen", [archived()]) == actions.Plan(MEMO, (), None)
    assert refused("reopen", [archived("cpa")]) == ("E_ALREADY", "filing")
    spawned = [archived(), hand(), {"ev": "queue.spawned", "at": AT, "qid": "q3"}]
    assert refused("reopen", spawned) == ("E_ALREADY", "queue")
    starting = [
        archived(),
        hand(),
        {"ev": "queue.done", "at": AT, "qid": "q3", "by": "undo"},
        {"ev": "queue.spawned", "at": AT, "qid": "q3"},
    ]  # withdrawn while the device was starting it
    assert refused("reopen", starting) == ("E_ALREADY", "queue")


def test_the_result_is_the_state_after_and_the_undo_only_when_something_changed():
    p = plan("done", [archived()])
    assert actions.result(p, (archived(), *p.records), KNOWN, changed=True) == {
        "ok": True, "v": 1, "id": MEMO, "filing": "done", "changed": True, "undo": p.undo, "queue": None}  # fmt: skip
    assert actions.result(p, (archived(),), KNOWN, changed=False)["undo"] is None


def test_undo_argv_is_rebuilt_from_the_neutral_object():
    assert actions.undo_argv({"op": "file", "id": MEMO, "args": {"project": None}}) == [
        "cpa", "filing", "file", MEMO, "--none"]  # fmt: skip
    assert actions.undo_argv({"op": "file", "id": MEMO, "args": {"project": "cpa"}})[-1] == "cpa"
    assert actions.undo_argv({"op": "reopen", "id": MEMO, "args": {}}) == ["cpa", "filing", "reopen", MEMO]


# --- every change, then its undo, gives the state back ------------------------------------------

STARTS = st.tuples(st.sampled_from([None, "cpa", "acme"]), st.booleans(), st.booleans(), st.booleans())
ASKS = st.sampled_from([("file", {"project": "cpa"}), ("file", {"project": "blog"}), ("file", {"none": True}),
                        ("done", {}), ("hide", {}), ("reopen", {}), ("hand", {}), ("hand", {"project": "blog"}),
                        ("hand", {"spawn": True})])  # fmt: skip


def _start(project, done, hidden, handed) -> list[dict]:
    extra = [{"ev": "rec.edited", "at": AT, "rec": MEMO, "done": True}] if done else []
    extra += [hand()] if handed else []
    return [archived(project), *extra, *([{"ev": "rec.hidden", "at": AT, "rec": MEMO}] if hidden else [])]


def _shape(f: ff.Filing) -> tuple:
    return f.filing, f.project, f.asked, f.done


@settings(max_examples=200, deadline=None)
@given(start=STARTS, ask=ASKS)
def test_any_change_followed_by_its_undo_restores_the_state(start, ask):
    events = _start(*start)
    op, fields = ask
    try:
        p = actions.plan(actions.Ask(op, MEMO, other=True, **fields), tuple(events), KNOWN, AT, "a transcript")
    except FilingError:
        return
    if not p.records or p.undo is None:
        return
    done = [*events, *p.records]
    back = actions.ask_of(p.undo)
    undone = actions.plan(back, tuple(done), KNOWN, AT)
    assert _shape(after(done, undone)) == _shape(after(events, actions.Plan(MEMO, (), None)))


def _stack_shape(events) -> tuple:
    f = ff.filing_of(index.fold(events), ff.fold(events), MEMO, KNOWN)
    return f.filing, f.project, f.asked, f.hand is not None and f.hand.open


@settings(max_examples=400, deadline=None)
@given(recorded=st.sampled_from([None, "cpa", "acme"]), asks=st.lists(ASKS, min_size=1, max_size=3))
def test_a_stack_of_changes_undone_newest_first_gives_every_state_back(recorded, asks):
    """As the Mac page's repeated ⌘Z and triage's u: every undo returns to the state before its own change."""
    events, before, undos = (archived(recorded),), [], []
    for op, fields in asks:
        try:
            p = actions.plan(actions.Ask(op, MEMO, other=True, **fields), events, KNOWN, AT, "a transcript")
        except FilingError:
            continue
        if p.records:
            before, undos, events = [*before, _stack_shape(events)], [*undos, p.undo], (*events, *p.records)
    for want, undo in zip(reversed(before), reversed(undos), strict=True):
        if undo is None:
            return
        events = (*events, *actions.plan(actions.ask_of(undo), events, KNOWN, AT).records)
        assert _stack_shape(events) == want
