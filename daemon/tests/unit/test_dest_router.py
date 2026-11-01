"""The router (docs/14 §6, §7.4): decisions become records to append, deliveries to run and replies to send."""

from __future__ import annotations

from dataclasses import dataclass, replace

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from cpad.agents.claude_code.argv import check_text
from cpad.dest import catalog as c
from cpad.dest import records as r
from cpad.dest import router
from cpad.dest.handoff import handed_text
from cpad.dest.model import VOICE_TEXT_MAX, Capture, Delivery, DeliveryResult, Dest, FileOpts, UrlOpts
from cpad.dest.pending import PendingState, find, fold_pending
from cpad.dest.router import Reply, RouteContext, on_capture, on_result, on_send, resume, route
from cpad.protocol.limits import LIMITS

ITEM = "3QZK1M7-0000C-0005"
AT = "2026-10-22T09:14:05.000+00:00"
LATER = "2026-10-23T09:14:06.000+00:00"
OPTIN = "2026-10-22T09:00:00+00:00"
SB = Dest("file.sb", "file", "reading-list", 7, key="o", leaves_mac=True, optin=OPTIN,
          file=FileOpts("~/sb", "inbox.md"))  # fmt: skip
CAT = c.put(c.defaults(), SB)


@dataclass(frozen=True)
class Send:
    id: str
    ref: str
    op: str
    dest: str | None = None


def cap(dest: str = "clip", source: str = "typed", **changes) -> Capture:
    return replace(Capture(ITEM, source, dest, "key", "cpa", "周五前给 Alice 发报价", "2026-10-22 09:14"), **changes)


def evs(step) -> list[str]:
    return [r["ev"] for r in step.records]


def received(capture: Capture) -> dict:
    body = {"text": capture.text, "project": capture.project, "dest": capture.dest}
    return {"ev": "item.received", "at": AT, "dev": "020000000001", "id": capture.ref, "kind": "note", "body": body,
            "at_dev": {"up_ms": 1, "wall_ms": None}, "received_at": AT}  # fmt: skip


def captured(capture: Capture, *, online: bool, origin: str = "queued") -> PendingState:
    """The pending state after the hub appended (item.received, *on_capture records)."""
    step = on_capture(CAT, capture, online=online, origin=origin, at=AT)
    return fold_pending([received(capture), *step.records])


# --- on_capture ------------------------------------------------------------------------------


def test_online_typed_clip_is_copied_now():
    step = on_capture(CAT, cap(), online=True, origin="live", at=AT)
    assert evs(step) == ["dest.decided"] and step.records[0]["action"] == "deliver"
    assert step.deliveries == (Delivery(f"{ITEM}:clip", cap(), c.find(CAT, "clip"), 1, "auto"),)
    assert step.reply is None


def test_offline_clip_is_held():
    step = on_capture(CAT, cap(), online=False, origin="queued", at=AT)
    assert step.records[0]["action"] == "hold" and step.records[0]["reason"] == "await_device"
    assert step.deliveries == ()


@pytest.mark.parametrize(("dest", "source", "intent", "voice"),
                         [("queue", "typed", "todo", False), ("queue", "voice", "todo", True),
                          ("session", "typed", "spawn", False), ("session", "voice", "spawn", True)])  # fmt: skip
def test_queue_and_session_become_queue_items(dest, source, intent, voice):
    step = on_capture(CAT, cap(dest, source), online=True, origin="live", at=AT)
    assert evs(step) == ["dest.decided", "queue.added"] and step.deliveries == ()
    added = step.records[1]
    assert (added["qid"], added["origin"], added["from_item"], added["project"]) == (None, "dest", ITEM, "cpa")
    assert (added["intent"], added.get("voice", False), added["text"]) == (intent, voice, cap().text)
    assert added["uncertain"] is False  # U25's flag, never speech (docs/22 §5.4 item 8)


LONG_MEMO = ("第一行\n第二行\t" + "很长的一段话" * 400)[:VOICE_TEXT_MAX]


@pytest.mark.parametrize("dest", ["queue", "session"])
def test_a_long_voice_memo_becomes_a_queue_item_that_can_be_spawned(dest):
    step = on_capture(CAT, cap(dest, "voice", text=LONG_MEMO), online=False, origin="queued", at=AT)
    text = step.records[1]["text"]
    assert check_text(text) is None and len(text.encode("utf-8")) <= LIMITS["text_max"]
    assert text.startswith("第一行 第二行 很长的一段话")
    assert LONG_MEMO.replace("\n", " ").replace("\t", " ").startswith(text)


@pytest.mark.parametrize("text", ["- rm -rf ~", "  -x", "\n\n\t"])
@pytest.mark.parametrize("dest", ["queue", "session"])
def test_a_text_that_could_never_be_spawned_is_downgraded(dest, text):
    step = on_capture(CAT, cap(dest, "voice", text=text), online=False, origin="queued", at=AT)
    assert evs(step) == ["dest.decided"] and step.deliveries == ()
    assert (step.records[0]["action"], step.records[0]["dest"], step.records[0]["reason"]) == (
        "downgrade", "daily", "bad_text")  # fmt: skip


@settings(max_examples=200)
@given(st.text(max_size=VOICE_TEXT_MAX), st.sampled_from(["queue", "session"]), st.sampled_from(["typed", "voice"]))
def test_every_queue_item_from_a_dest_can_be_spawned(text, dest, source):
    step = on_capture(CAT, cap(dest, source, text=text), online=True, origin="live", at=AT)
    added = [rec for rec in step.records if rec["ev"] == "queue.added"]
    assert all(check_text(rec["text"]) is None for rec in added)
    assert added or step.records[0]["action"] == "downgrade"


@pytest.mark.parametrize("capture", [cap("mail.x"), cap("file.gone"), cap("paste"), cap("queue", project=None),
                                     cap("todo")])  # fmt: skip
def test_downgrades_and_archives_only_record(capture):
    step = on_capture(CAT, capture, online=True, origin="live", at=AT)
    assert evs(step) == ["dest.decided"] and step.deliveries == ()


def test_file_is_delivered_even_offline():
    step = on_capture(CAT, cap("file.sb"), online=False, origin="queued", at=AT)
    assert [d.dest for d in step.deliveries] == [SB]


def test_route_is_on_capture_with_a_context():
    ctx = RouteContext(CAT, online=True, origin="live", at=AT)
    assert route(cap(), ctx) == on_capture(CAT, cap(), online=True, origin="live", at=AT)


# --- on_send ---------------------------------------------------------------------------------

HELD = captured(cap(), online=False)


def test_go_delivers_as_confirmed_and_answers_later():
    step = on_send(CAT, HELD, Send("d21", ITEM, "go"), online=True, at=AT)
    assert step.records[0]["origin"] == "confirmed" and step.records[0]["action"] == "deliver"
    (delivery,) = step.deliveries
    assert (delivery.by, delivery.re, delivery.attempt) == ("device", "d21", 1)
    assert step.reply is None


def test_go_with_a_dest_reroutes_as_a_pick():
    step = on_send(CAT, HELD, Send("d23", ITEM, "go", "file.sb"), online=True, at=AT)
    decided = step.records[0]
    assert (decided["want"], decided["dest"], decided["how"]) == ("file.sb", "file.sb", "pick")
    assert step.deliveries[0].dest == SB and step.deliveries[0].capture.dest == "file.sb"


def test_reroute_to_a_queue_answers_at_once():
    step = on_send(CAT, HELD, Send("d23", ITEM, "go", "queue"), online=True, at=AT)
    assert evs(step) == ["dest.decided", "queue.added"] and step.reply == Reply()
    assert find(fold_pending(step.records, HELD), ITEM) is None


def test_reroute_to_todo_is_refused_and_records_nothing():
    """No checkbox line can be written for an item already in the daily note yet (the hub has no such effect)."""
    step = on_send(CAT, HELD, Send("d23", ITEM, "go", "todo"), online=True, at=AT)
    assert step == router.Step(reply=Reply("E_BAD_FIELD", "dest"))
    assert find(fold_pending(step.records, HELD), ITEM).st == "hold"


def test_reroute_to_daily_keeps_only_the_line_already_written():
    step = on_send(CAT, HELD, Send("d23", ITEM, "go", "daily"), online=True, at=AT)
    assert step.records[0]["action"] == "deliver" and step.records[0]["dest"] == "daily"
    assert step.deliveries == () and step.reply == Reply()


@pytest.mark.parametrize("dest", ["mail.x", "file.gone", "paste", "plugin.x"])
def test_reroute_to_an_unusable_dest_is_a_bad_field(dest):
    off = c.remove(CAT, "clip")
    for cat, target in ((CAT, dest), (off, "clip")):
        step = on_send(cat, HELD, Send("d23", ITEM, "go", target), online=True, at=AT)
        assert step == router.Step(reply=Reply("E_BAD_FIELD", "dest"))


def test_drop():
    step = on_send(CAT, HELD, Send("d25", ITEM, "drop"), online=True, at=AT)
    assert step.records == ({"ev": "dest.dropped", "at": AT, "item": ITEM, "dest": "clip", "by": "device",
                             "prof": None},)  # fmt: skip
    assert step.reply == Reply() and step.deliveries == ()


def test_unknown_ref_and_running_item():
    unknown = on_send(CAT, HELD, Send("d1", "3QZK1M7-0000C-0099", "go"), online=True, at=AT)
    assert unknown == router.Step(reply=Reply("E_UNKNOWN_ITEM", "ref"))
    running = fold_pending(on_send(CAT, HELD, Send("d2", ITEM, "go"), online=True, at=AT).records, HELD)
    busy = on_send(CAT, running, Send("d3", ITEM, "drop"), online=True, at=AT)
    assert busy == router.Step(reply=Reply("E_BUSY"))


def test_go_on_a_dest_removed_meanwhile_downgrades_and_says_so():
    """Not an ack: the device must not report a copy or a delivery that never happened."""
    gone = captured(cap("file.sb"), online=False)
    failed = fold_pending([{"ev": "dest.failed", "at": AT, "item": ITEM, "dest": "file.sb", "attempt": 1,
                            "code": "E_DEST_IO", "retryable": True}], gone)  # fmt: skip
    step = on_send(c.remove(CAT, "file.sb"), failed, Send("d4", ITEM, "go"), online=True, at=AT)
    assert step.records[0]["action"] == "downgrade" and step.deliveries == ()
    assert step.reply == Reply("E_BAD_FIELD", "dest")
    assert find(fold_pending(step.records, failed), ITEM) is None


def test_go_on_a_clip_turned_off_meanwhile_is_not_acked():
    step = on_send(c.remove(CAT, "clip"), HELD, Send("d4", ITEM, "go"), online=True, at=AT)
    assert (step.records[0]["action"], step.records[0]["reason"]) == ("downgrade", "disabled")
    assert step.reply == Reply("E_BAD_FIELD", "dest") and step.deliveries == ()


def test_reroute_without_a_project_downgrades_and_says_so():
    orphan = captured(cap(project=None), online=False)
    step = on_send(CAT, orphan, Send("d4", ITEM, "go", "session"), online=True, at=AT)
    assert (step.records[0]["reason"], step.reply) == ("no_project", Reply("E_BAD_FIELD", "dest"))


# --- on_appended: the memo hook ---------------------------------------------------------------

REC = "K3M9Q2T-00042-0A1C"


def memo(dest: str = "queue", **extra) -> dict:
    return {"ev": "rec.result", "at": AT, "rec": REC, "state": "done", "level": "summary", "title": "t",
            "memo_text": "买 USB-C hub，顺便看看扩展坞", "mode": "memo", "project": "cpa", "started_ms": 1761124440000,
            "uncertain": False, "dur_ms": 9000, "dest": dest, **extra}  # fmt: skip


def appended(records, pend: PendingState | None = None):
    return router.on_appended(CAT, pend or PendingState(), records, online=False, at=LATER)


def test_a_new_memo_is_routed_once():
    pend, step = appended([memo()])
    assert evs(step) == ["dest.decided", "queue.added"] and step.records[1]["from_item"] == REC
    assert (step.records[0]["src"], step.records[0]["origin"]) == ("voice", "queued")
    after, again = appended(step.records, pend)
    assert again == router.Step() and after == PendingState()


@pytest.mark.parametrize("dest", ["queue", "session", "clip", "file.sb", "todo"])
def test_a_restart_never_routes_a_decided_memo_again(dest):
    """At start-up the hub's one Appended carries the whole events.jsonl, rec.result included."""
    _, first = appended([memo(dest)])
    history = [memo(dest), *first.records]
    for _ in range(3):  # every restart
        pend, step = appended(history)
        assert step == router.Step() and pend.captures == {}
        history = [*history, *step.records]


@pytest.mark.parametrize("final", ["dest.dropped", "dest.expired"])
def test_a_dropped_or_expired_memo_stays_gone(final):
    _, held = appended([memo("clip")])
    outcome = {"ev": final, "at": LATER, "item": REC, "dest": "clip"}
    pend, step = appended([memo("clip"), *held.records, outcome])
    assert step == router.Step() and pend == PendingState()


def test_a_memo_cut_off_by_a_crash_is_routed_at_start_up():
    """rec.result is appended on its own; a crash before its dest.decided leaves it undecided in the history."""
    pend, step = appended([memo("clip")])
    assert step.records[0]["action"] == "hold" and step.deliveries == ()
    assert find(fold_pending(step.records, pend), REC).st == "hold"


def test_typed_notes_and_memos_without_a_dest_are_never_routed_from_appended():
    v041_todo = received(cap("todo"))  # remembered, never decided: v0.4.1 wrote the checkbox itself
    _, step = appended([v041_todo, memo("daily"), memo(None), memo("queue", state="failed")])
    assert step == router.Step()


def test_the_memo_hook_routes_every_undecided_memo_in_order():
    second = memo("queue", rec="K3M9Q2T-00042-0A1D")
    _, step = appended([memo("file.sb"), second])
    assert [rec["item"] for rec in step.records if rec["ev"] == "dest.decided"] == [REC, "K3M9Q2T-00042-0A1D"]
    assert [d.capture.ref for d in step.deliveries] == [REC]


# --- on_result -------------------------------------------------------------------------------


def test_a_handover_is_recorded_as_handed_with_what_the_app_received():
    url = UrlOpts("test-scheme://a?q={text}", "b.c", None, effect="draft", verified="draft")
    grok = Dest("url.g", "url", "G", 8, leaves_mac=True, optin=AT, url=url)
    d = Delivery(f"{ITEM}:url.g", cap("url.g"), grok, 1, "cli")
    step = on_result(d, DeliveryResult(True, ref="too_long", via="clip"), at=AT)
    (handed,) = step.records
    assert (handed["ev"], handed["via"], handed["ref"], handed["leaves_mac"]) == (
        "dest.handed",
        "clip",
        "too_long",
        True,
    )
    assert (handed["bytes"], handed["sha8"]) == r.digest(handed_text(cap("url.g")))
    assert step.reply is None


def test_results():
    d = Delivery(f"{ITEM}:clip", cap(), c.find(CAT, "clip"), 1, "device", re="d21")
    ok = on_result(d, DeliveryResult(True), at=AT)
    assert evs(ok) == ["dest.delivered"] and ok.reply == Reply()
    bad = on_result(d, DeliveryResult(False, "E_DEST_IO", True), at=AT)
    assert evs(bad) == ["dest.failed"] and bad.reply == Reply("E_INTERNAL")
    assert bad.records[0]["retryable"] is True
    assert on_result(replace(d, re=None), DeliveryResult(True, ref="^cpa-x"), at=AT).reply is None
    assert on_result(replace(d, re=None), DeliveryResult(True, ref="^cpa-x"), at=AT).records[0]["ref"] == "^cpa-x"


def test_a_failure_without_a_code_is_internal():
    d = Delivery(f"{ITEM}:clip", cap(), c.find(CAT, "clip"))
    assert on_result(d, DeliveryResult(False), at=AT).records[0]["code"] == "E_INTERNAL"


# --- expire / resume -------------------------------------------------------------------------


def _at(hours: float) -> float:
    from datetime import datetime  # noqa: PLC0415

    return datetime.fromisoformat(AT).timestamp() + hours * 3600


def test_expire_only_items_older_than_a_day():
    state = captured(cap(), online=False)
    assert router.expire(state, now=_at(23.9), at=LATER).records == ()
    step = router.expire(state, now=_at(24.1), at=LATER)
    assert step.records == ({"ev": "dest.expired", "at": LATER, "item": ITEM, "dest": "clip", "prof": None},)


def test_running_items_never_expire():
    running = captured(cap("file.sb"), online=True, origin="live")
    assert router.expire(running, now=_at(48), at=LATER).records == ()


def _failed(state: PendingState, attempt: int, retryable: bool, dest: str = "file.sb") -> PendingState:
    return fold_pending([{"ev": "dest.failed", "at": AT, "item": ITEM, "dest": dest, "attempt": attempt,
                          "code": "E_DEST_IO", "retryable": retryable}], state)  # fmt: skip


def test_resume_redelivers_running_files_and_interrupts_the_clipboard():
    files = captured(cap("file.sb"), online=True, origin="live")
    again = resume(files, CAT, at=LATER, startup=True)
    assert evs(again) == ["dest.decided"] and again.deliveries[0].attempt == 2
    clip = captured(cap(), online=True, origin="live")
    stopped = resume(clip, CAT, at=LATER, startup=True)
    assert stopped.deliveries == () and stopped.records[0]["ev"] == "dest.failed"
    assert (stopped.records[0]["code"], stopped.records[0]["retryable"]) == ("E_INTERRUPTED", False)
    assert find(fold_pending(stopped.records, clip), ITEM).st == "failed"


def test_resume_retries_retryable_archive_failures_up_to_three_attempts():
    files = captured(cap("file.sb"), online=True, origin="live")
    assert len(resume(_failed(files, 1, True), CAT, at=LATER, startup=True).deliveries) == 1
    assert len(resume(_failed(files, 2, True), CAT, at=LATER, startup=True).deliveries) == 1
    assert resume(_failed(files, 3, True), CAT, at=LATER, startup=True) == router.Step()
    assert resume(_failed(files, 1, False), CAT, at=LATER, startup=True) == router.Step()


def test_resume_never_retries_a_failed_clipboard_or_a_hold():
    held = captured(cap(), online=False)
    assert resume(held, CAT, at=LATER, startup=True) == router.Step()
    assert resume(_failed(held, 1, True, "clip"), CAT, at=LATER, startup=True) == router.Step()


def test_resume_downgrades_a_file_dest_removed_meanwhile():
    files = captured(cap("file.sb"), online=True, origin="live")
    step = resume(files, c.remove(CAT, "file.sb"), at=LATER, startup=True)
    assert step.records[0]["action"] == "downgrade" and step.deliveries == ()
    assert fold_pending(step.records, files) == PendingState()


# --- a whole life ----------------------------------------------------------------------------


def test_offline_clip_waits_then_is_copied_on_enter():
    state = captured(cap(), online=False)
    assert find(state, ITEM).st == "hold"
    go = on_send(CAT, state, Send("d21", ITEM, "go"), online=True, at=AT)
    state = fold_pending(go.records, state)
    done = on_result(go.deliveries[0], DeliveryResult(True), at=AT)
    assert fold_pending(done.records, state) == PendingState() and done.reply == Reply()


def test_resume_has_no_default_for_startup():
    """A link-up treated as a start-up would deliver a running file twice and interrupt a running copy."""
    with pytest.raises(TypeError):
        resume(HELD, CAT, at=LATER)  # type: ignore[call-arg]


def test_a_delivery_decision_carries_its_attempt():
    assert on_capture(CAT, cap(), online=True, origin="live", at=AT).records[0]["attempt"] == 1
    assert "attempt" not in on_capture(CAT, cap(), online=False, origin="queued", at=AT).records[0]
    files = captured(cap("file.sb"), online=True, origin="live")
    again = resume(files, CAT, at=LATER, startup=True)
    assert again.records[0]["attempt"] == again.deliveries[0].attempt == 2
    assert find(fold_pending(again.records, files), ITEM).attempt == 2


def test_link_up_resume_leaves_running_deliveries_alone():
    files = captured(cap("file.sb"), online=True, origin="live")
    clip = captured(cap(), online=True, origin="live")
    assert resume(files, CAT, at=LATER, startup=False) == router.Step()
    assert resume(clip, CAT, at=LATER, startup=False) == router.Step()
    assert len(resume(_failed(files, 1, True), CAT, at=LATER, startup=False).deliveries) == 1


def test_an_unknown_op_is_a_bad_field():
    assert on_send(CAT, HELD, Send("d9", ITEM, "copy"), online=True, at=AT).reply == Reply("E_BAD_FIELD", "op")
