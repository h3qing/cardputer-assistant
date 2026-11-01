"""fold_pending (docs/14 §6.2): what waits on the device, folded from events.jsonl like the queue."""

from __future__ import annotations

from datetime import UTC

import pytest
from hypothesis import given
from hypothesis import strategies as st

from cpad.dest.capture import capture_from_item
from cpad.dest.pending import CAPTURES_MAX, PendingState, find, fold_pending, visible

ITEM = "3QZK1M7-0000C-0005"
OTHER = "3QZK1M7-0000C-0006"
AT = "2026-10-22T09:14:05.000+00:00"


def received(item: str = ITEM, dest: str = "clip", text: str = "周五前给 Alice 发报价") -> dict:
    return {"ev": "item.received", "at": AT, "dev": "020000000001", "id": item, "kind": "note",
            "body": {"text": text, "project": "cpa", "dest": dest}, "at_dev": {"up_ms": 5, "wall_ms": None},
            "received_at": AT}  # fmt: skip


def decided(action: str, dest: str = "clip", item: str = ITEM, at: str = AT, **extra) -> dict:
    kind = dest.partition(".")[0]
    return {"ev": "dest.decided", "at": at, "item": item, "src": "typed", "want": dest, "dest": dest, "kind": kind,
            "action": action, "reason": "x", "how": "key", "origin": "queued", **extra}  # fmt: skip


def outcome(ev: str, dest: str = "clip", item: str = ITEM, **extra) -> dict:
    return {"ev": ev, "at": "2026-10-22T09:15:00.000+00:00", "item": item, "dest": dest, **extra}


def failed(attempt: int = 1, retryable: bool = False, code: str = "E_DEST_IO", dest: str = "clip") -> dict:
    return outcome("dest.failed", dest, kind=dest.partition(".")[0], code=code, retryable=retryable, attempt=attempt)


def test_a_capture_is_remembered_until_it_is_decided():
    state = fold_pending([received()])
    assert set(state.captures) == {ITEM} and state.items == ()
    assert fold_pending([received(dest="daily")]).captures == {}


def test_hold_becomes_a_visible_item_with_its_capture():
    state = fold_pending([received(), decided("hold")])
    item = find(state, ITEM)
    assert item.st == "hold" and item.dest == "clip" and item.since == AT and item.attempt == 0
    assert item.capture == capture_from_item(received())
    assert visible(state) == (item,) and state.captures == {}


def test_a_deliver_through_an_adapter_is_running_and_not_visible():
    state = fold_pending([received(), decided("deliver")])
    assert find(state, ITEM).st == "running" and find(state, ITEM).attempt == 1
    assert visible(state) == ()


@pytest.mark.parametrize(
    "decision",
    [decided("deliver", "todo"), decided("deliver", "daily"), decided("queue", "queue", intent="todo"),
     decided("downgrade", "daily")],
)  # fmt: skip
def test_decisions_without_a_delivery_are_final(decision):
    state = fold_pending([received(), decision])
    assert state == PendingState()


@pytest.mark.parametrize("final", ["dest.delivered", "dest.handed", "dest.dropped", "dest.expired", "dest.downgraded"])
def test_final_outcomes_forget_item_and_text(final):
    state = fold_pending([received(), decided("hold"), outcome(final)])
    assert state.items == () and state.captures == {}


def test_failure_keeps_the_item_visible_and_a_retry_runs_again():
    events = [received(dest="file.sb"), decided("deliver", "file.sb"), failed(1, True, dest="file.sb")]
    item = find(fold_pending(events), ITEM)
    assert (item.st, item.attempt, item.code, item.retryable) == ("failed", 1, "E_DEST_IO", True)
    assert item.since == "2026-10-22T09:15:00.000+00:00"
    again = find(fold_pending([*events, decided("deliver", "file.sb", origin="confirmed")]), ITEM)
    assert (again.st, again.attempt) == ("running", 2)


def test_a_reroute_changes_the_dest_and_how():
    events = [received(), decided("hold"), decided("deliver", "file.sb", how="pick")]
    item = find(fold_pending(events), ITEM)
    assert item.dest == "file.sb" and item.capture.dest == "file.sb" and item.capture.how == "pick"


def test_outcomes_for_unknown_items_change_nothing():
    events = [decided("hold"), failed(), outcome("dest.delivered")]
    assert fold_pending(events) == PendingState()


def test_items_stay_oldest_first():
    events = [received(), received(OTHER), decided("hold"), decided("hold", item=OTHER), failed()]
    events += [decided("hold", item=ITEM)]
    assert [i.capture.ref for i in fold_pending(events).items] == [ITEM, OTHER]


def test_captures_are_bounded():
    events = [received(f"3QZK1M7-0000C-{n:04d}") for n in range(CAPTURES_MAX + 20)]
    state = fold_pending(events)
    assert len(state.captures) == CAPTURES_MAX and "3QZK1M7-0000C-0000" not in state.captures


def test_unrelated_events_are_ignored():
    assert fold_pending([{"ev": "queue.added", "qid": "q1"}, {}, {"ev": "dest.decided"}]) == PendingState()


LIFE = [
    received(), received(OTHER, "file.sb"), decided("hold"), decided("deliver", "file.sb", item=OTHER, attempt=1),
    failed(1, True, dest="file.sb") | {"item": OTHER}, decided("deliver", "clip", origin="confirmed", attempt=1),
    outcome("dest.delivered"), decided("deliver", "file.sb", item=OTHER, attempt=2),
    received("3QZK1M7-0000C-0007", "queue"),
]  # fmt: skip


@given(st.integers(min_value=0, max_value=len(LIFE)))
def test_folding_is_incremental(cut):
    assert fold_pending(LIFE) == fold_pending(LIFE[cut:], fold_pending(LIFE[:cut]))


def test_folding_is_deterministic():
    once = fold_pending(LIFE)
    assert fold_pending(LIFE) == once
    assert [(i.capture.ref, i.st, i.attempt) for i in once.items] == [(OTHER, "running", 2)]
    assert set(once.captures) == {"3QZK1M7-0000C-0007"}


@given(st.integers(min_value=0, max_value=len(LIFE)))
def test_folding_events_onto_the_state_they_made_changes_nothing(cut):
    """The fold is idempotent: a decision that starts a delivery carries its attempt, it is not counted up."""
    once = fold_pending(LIFE[:cut])
    assert fold_pending(LIFE[:cut], once) == once


def test_a_running_item_folded_twice_keeps_its_attempt():
    events = [received(dest="file.sb"), decided("deliver", "file.sb", attempt=1)]
    twice = fold_pending(events, fold_pending(events))
    assert (find(twice, ITEM).st, find(twice, ITEM).attempt) == ("running", 1)


def test_a_decision_without_an_attempt_counts_up():
    events = [received(dest="file.sb"), decided("deliver", "file.sb"), failed(1, True, dest="file.sb")]
    assert find(fold_pending([*events, decided("deliver", "file.sb")]), ITEM).attempt == 2
    assert find(fold_pending([*events, decided("deliver", "file.sb", attempt=True)]), ITEM).attempt == 2


def test_receipt_time_is_used_in_the_stamp():
    assert find(fold_pending([received(), decided("hold")]), ITEM).capture.stamp.startswith("~")
    assert capture_from_item(received(), UTC).stamp == "~2026-10-22 09:14"


def test_a_decision_without_a_dest_is_ignored():
    odd = {k: v for k, v in decided("hold").items() if k != "dest"}
    state = fold_pending([received(), odd])
    assert state.items == () and set(state.captures) == {ITEM}
    assert find(fold_pending([received(), decided("hold", how=7)]), ITEM).capture.how == "key"


# --- sent from this Mac (docs/25 §5.4): the text stays in rec.result, the fold keeps a skeleton -------------------


def test_a_mac_send_of_a_memo_without_a_capture_is_tracked_without_text():
    events = [decided("deliver", "url.grok", src="voice", how="pick", origin="mac", prof="home", attempt=1)]
    item = find(fold_pending(events), ITEM)
    assert (item.st, item.dest, item.attempt) == ("running", "url.grok", 1)
    cap = item.capture
    assert (cap.ref, cap.source, cap.dest, cap.how, cap.text, cap.prof) == (ITEM, "voice", "url.grok", "pick", None,
                                                                             "home")  # fmt: skip
    failing = find(fold_pending([*events, failed(1, dest="url.grok", code="E_TOO_LONG")]), ITEM)
    assert (failing.st, failing.code) == ("failed", "E_TOO_LONG")
    assert fold_pending([*events, outcome("dest.handed", "url.grok")]).items == ()


def test_only_a_mac_decision_starts_a_skeleton():
    assert fold_pending([decided("deliver", "url.grok", origin="confirmed")]).items == ()
    odd = decided("deliver", "url.grok", origin="mac", src="nope", how="nope", prof=7)
    cap = find(fold_pending([odd]), ITEM).capture
    assert (cap.source, cap.how, cap.prof) == ("typed", "app", None)
