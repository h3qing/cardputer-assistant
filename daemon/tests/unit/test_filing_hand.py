"""cpa filing hand (docs/23 §3.4): one queue.added{origin:"inbox", from_item, voice:true, bytes, sha8} whose text is
taken from the recording itself (its memo text, else its transcript), cleaned by queue_text() and cut to 500 B,
never from argv; meetings are refused (D16); reopen withdraws it with queue.done{by:"undo"} (D17). Pure."""

from __future__ import annotations

import hashlib

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.agents.claude_code.argv import check_text
from cpad.events import records as queue_records
from cpad.filing import actions
from cpad.filing import fold as ff
from cpad.filing.args import FilingError
from cpad.filing.text import queue_text
from cpad.projects.registry import Project
from cpad.queue.fold import fold_queue
from cpad.recordings import index
from cpad.recordings import records as r

DEV = "020000000001"
AT = "2026-10-22T03:23:00.000+00:00"
MEMO = "3QZK1M7-0000A-0001"
KNOWN = ff.Known.of((Project("cpa", "Cardputer", 1, "/r"), Project("blog", "Blog", 2, "/b")))
SAID = "把设置页的 WiFi 部分改成英文。"


def archived(project: str | None = None, mode: str = "memo") -> dict:
    return r.archived(dev=DEV, rec=MEMO, dir=f"recordings/2026-10-22/1123-{MEMO.lower()}", mode=mode,
                      project=project, segments=1, dur_ms=24000, started_ms=1792639380000, uncertain=False, gaps=[],
                      consent=None, at=AT)  # fmt: skip


def result(session: dict, memo_text: str | None) -> dict:
    return r.result(rec=MEMO, state="done", level="summary", title=None, memo_text=memo_text, memo_intent=None,
                    actions_n=0, error_code=None, session=session, at=AT)  # fmt: skip


def memo(project: str | None = None, memo_text: str | None = SAID) -> list[dict]:
    session = archived(project)
    return [session, result(session, memo_text)]


def other_item(qid: str = "q3", **over) -> dict:
    return {**queue_records.queue_added(qid=qid, project="cpa", text="typed", intent="todo", origin="device", at=AT),
            **over}  # fmt: skip


def hand(events, transcript: str | None = None, **ask) -> actions.Plan:
    return actions.plan(actions.Ask("hand", ask.pop("rec", MEMO.lower()), **ask), tuple(events), KNOWN, AT,
                        transcript)  # fmt: skip


def after(events, p: actions.Plan) -> ff.Filing:
    found = (*events, *p.records)
    return ff.filing_of(index.fold(found), ff.fold(found), MEMO, KNOWN)


def refused(events, transcript: str | None = None, **ask) -> tuple[str, str | None]:
    with pytest.raises(FilingError) as err:
        hand(events, transcript, **ask)
    return err.value.code, err.value.field


# --- queue_text ---------------------------------------------------------------------------------


def test_queue_text_turns_controls_into_spaces_drops_bidi_and_trims():
    assert queue_text("  a\tb\x1b[31mc\x85d e‮f⁦g  ") == "a b [31mc d efg"


def test_queue_text_cuts_to_500_bytes_on_a_character_boundary_with_no_suffix():
    cut = queue_text("语" * 200)
    assert cut == "语" * 166 and len(cut.encode()) == 498
    assert queue_text("a" * 600) == "a" * 500


@pytest.mark.parametrize("text", [None, "", "   \n\t", "-x", "  --dangerously-skip-permissions", "‮-x"])
def test_queue_text_refuses_what_could_never_be_spawned(text):
    assert queue_text(text) is None


@settings(max_examples=300, deadline=None)
@given(st.text())
def test_any_queue_text_can_be_spawned(text):
    cut = queue_text(text)
    assert cut is None or (check_text(cut) is None and cut == cut.strip() and len(cut.encode()) <= 500)


# --- the record ---------------------------------------------------------------------------------


def test_hand_writes_one_inbox_queue_item_with_the_memo_text_and_its_size_and_digest():
    events = [*memo("cpa"), other_item("q3")]
    p = hand(events, transcript="ignored: the memo text wins")
    body = SAID.encode()
    assert p.records == ({"ev": "queue.added", "at": AT, "qid": "q4", "project": "cpa", "text": SAID,
                          "intent": "todo", "sid": None, "origin": "inbox", "from_item": MEMO, "uncertain": False,
                          "voice": True, "bytes": len(body),
                          "sha8": hashlib.sha256(body).hexdigest()[:8]},)  # fmt: skip
    assert p.undo == {"op": "reopen", "id": MEMO, "args": {}}
    f = after(events, p)
    assert (f.filing, f.project, f.hand) == ("handed", "cpa", ff.Hand("q4", "todo", "cpa"))


def test_spawn_only_queues_an_item_to_start_on_the_device_board():
    assert hand(memo("cpa"), spawn=True).records[0]["intent"] == "spawn"


@pytest.mark.parametrize("memo_text", [None, "", " \n "])
def test_without_memo_text_the_transcript_is_the_text(memo_text):
    p = hand(memo("cpa", memo_text=memo_text), transcript="  第一句。 第二句。\x07 ")
    assert p.records[0]["text"] == "第一句。 第二句。"


@pytest.mark.parametrize(("memo_text", "transcript"), [(None, None), (None, "   "), ("-rf /", None), (None, "--x")])
def test_no_usable_text_is_no_text(memo_text, transcript):
    assert refused(memo("cpa", memo_text=memo_text), transcript) == ("E_NO_TEXT", "text")


# --- the project --------------------------------------------------------------------------------


def test_the_key_defaults_to_the_effective_project_and_a_given_key_wins():
    assert hand(memo("cpa")).records[0]["project"] == "cpa"
    edited = [*memo("cpa"), r.edited_project(rec=MEMO, project="blog", at=AT)]
    assert hand(edited).records[0]["project"] == "blog"
    assert hand(memo(), project="blog").records[0]["project"] == "blog"


@pytest.mark.parametrize(
    ("project", "ask", "want"),
    [
        (None, {}, ("E_UNKNOWN_PROJECT", "project")),
        ("acme", {}, ("E_UNKNOWN_PROJECT", "project")),
        (None, {"project": "acme"}, ("E_UNKNOWN_PROJECT", "project")),
        (None, {"project": "Cpa"}, ("E_BAD_ARG", "project")),
        (None, {"project": "Cpa", "rec": "3QZK1M7-0000A-0009"}, ("E_BAD_ARG", "project")),
        (None, {"project": "cpa", "rec": "nope"}, ("E_BAD_ARG", "id")),
        (None, {"project": "cpa", "rec": "3QZK1M7-0000A-0009"}, ("E_UNKNOWN_ID", "id")),
        ("acme", {"project": "cpa"}, ("E_BAD_ARG", "other")),
    ],
)
def test_project_refusals(project, ask, want):
    assert refused(memo(project), **ask) == want


def test_the_other_mac_group_hands_with_other_and_reopen_gives_its_key_back():
    events = memo("acme")
    p = hand(events, project="cpa", other=True)
    assert after(events, p).filing == "handed"
    back = actions.plan(actions.ask_of(p.undo), (*events, *p.records), KNOWN, AT)
    restored = after([*events, *p.records], back)
    assert (restored.filing, restored.asked) == ("new", "acme")


# --- what the recording's state allows ----------------------------------------------------------


def test_a_meeting_is_never_handed():
    session = archived("cpa", mode="meeting")
    assert refused([session, result(session, None)], "我们先过一下 pricing。") == ("E_BAD_ARG", "id")


def test_one_open_item_at_a_time_and_a_hidden_recording_is_not_handed():
    first = hand(memo("cpa"))
    assert refused([*memo("cpa"), *first.records]) == ("E_ALREADY", "queue")
    assert refused([*memo("cpa"), r.hidden(rec=MEMO, at=AT)]) == ("E_ALREADY", "filing")


def test_after_its_item_was_started_the_recording_can_be_handed_again():
    started = [*memo("cpa"), *hand(memo("cpa")).records, queue_records.queue_spawned("q1", AT, "cpa/x", "2e7b")]
    again = hand(started, spawn=True)
    assert again.records[0]["qid"] == "q2" and after(started, again).hand == ff.Hand("q2", "spawn", "cpa")


@pytest.mark.parametrize(
    ("extra", "state"),
    [([], "filed"), ([r.edited_done(rec=MEMO, done=True, at=AT)], "done")],
    ids=["filed", "done"],
)
def test_reopen_withdraws_the_item_and_gives_the_state_before_back(extra, state):
    events = [*memo("cpa" if state == "filed" else None), *extra]
    p = hand(events, project="cpa")
    handed = [*events, *p.records]
    back = actions.plan(actions.ask_of(p.undo), tuple(handed), KNOWN, AT)
    assert back.records == ({"ev": "queue.done", "at": AT, "qid": "q1", "by": "undo"},)
    assert after(handed, back).filing == state
    assert fold_queue([*handed, *back.records]).items == (), "older folds close the item alike"


def test_the_result_carries_the_queue_item():
    p = hand(memo("cpa"))
    done = (*memo("cpa"), *p.records)
    assert actions.result(p, done, KNOWN, changed=True)["queue"] == {"qid": "q1", "intent": "todo", "open": True}
    assert actions.result(actions.Plan(MEMO, (), None), tuple(memo()), KNOWN, changed=False)["queue"] is None


def test_the_record_constructor_measures_the_utf8_body():
    rec = queue_records.queue_handed(qid="q9", project="cpa", text="语音", intent="spawn", rec=MEMO, at=AT)
    sha8 = hashlib.sha256("语音".encode()).hexdigest()[:8]
    assert (rec["bytes"], rec["sha8"], rec["voice"], rec["origin"]) == (6, sha8, True, "inbox")
