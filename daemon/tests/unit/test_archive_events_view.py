"""Views of events.jsonl for the archive (docs/15 §3.4): captures, the open queue, hidden recordings. Pure."""

from __future__ import annotations

from datetime import date, timedelta, timezone

from cpad.archive import events_view as ev
from cpad.archive.events_view import Capture
from cpad.recordings import records as r

TZ = timezone(timedelta(hours=-7))
DEV = "020000000001"
DAY = "2026-10-21"


def item(id_, kind, body, wall_ms=None, received="2026-10-21T22:00:00.000+00:00", orphan=False):
    rec = {"ev": "item.received", "at": received, "dev": DEV, "id": id_, "kind": kind, "body": body,
           "at_dev": {"up_ms": 1000, "wall_ms": wall_ms}, "received_at": received}  # fmt: skip
    return {**rec, "orphan": True} if orphan else rec


def memo_result(rec, text, intent="note", state="done", mode="memo", started_ms=1792620300000):
    session = r.archived(
        dev=DEV,
        rec=rec,
        dir="recordings/x",
        mode=mode,
        project="cpa",
        segments=1,
        dur_ms=12000,
        started_ms=started_ms,
        uncertain=False,
        gaps=[],
        consent=None,
        at="2026-10-21T22:10:00+00:00",
    )
    return r.result(rec=rec, state=state, level="summary", title="t", memo_text=text, memo_intent=intent, actions_n=0,
                    error_code=None, session=session, at="2026-10-21T22:10:00+00:00")  # fmt: skip


EVENTS = (
    item("3QZK1M7-0000C-0005", "note", {"text": "买牛奶", "project": "cpa"}, wall_ms=1792620000000),
    item(
        "3QZK1M7-0000C-0006",
        "note",
        {"text": "no clock", "project": "home", "dest": "todo"},
        received="2026-10-21T21:59:00.000+00:00",
    ),
    item("3QZK1M7-0000C-0007", "note", {"text": "orphan", "project": "cpa"}, wall_ms=1792533600000, orphan=True),
    item(
        "3QZK1M7-0000C-0008",
        "queue",
        {"text": "fix the build", "project": "cpa", "intent": "do", "sid": None, "uncertain": False},
        wall_ms=1792620060000,
    ),  # fmt: skip
    memo_result("3QZK1M7-00042-0A1F", "周五前发报价单", intent="todo"),
    memo_result("3QZK1M7-00042-0A1C", None, mode="meeting"),
    memo_result("3QZK1M7-00042-0A1D", "failed one", state="failed"),
    {"ev": "item.received", "id": "bad", "kind": "note"},
    item("3QZK1M7-0000C-0009", "note", {"text": 5, "project": "cpa"}),
    item("3QZK1M7-0000C-000A", "photo", {"text": "x"}),
    {"ev": "agent.spawned", "project": "cpa"},
)


def test_captures_newest_first_with_local_times():
    found = ev.captures(EVENTS, tz=TZ)
    assert [c.id[-4:] for c in found] == ["0A1F", "0008", "0005", "0006", "0007"]
    assert found[2] == Capture("3QZK1M7-0000C-0005", "note", f"{DAY}T15:00:00-07:00", "cpa", None, "买牛奶")
    assert found[0] == Capture("3QZK1M7-00042-0A1F", "memo", f"{DAY}T15:05:00-07:00", "cpa", "todo", "周五前发报价单")
    assert found[1].kind == "queue" and found[1].text == "fix the build"
    assert found[3].at == "2026-10-21T14:59:00-07:00" and found[3].dest == "todo"  # received_at, no device clock
    assert found[4].project is None  # orphan


def test_capture_keys_sort_like_the_list():
    found = ev.captures(EVENTS, tz=TZ)
    assert [c.key for c in found] == sorted((c.key for c in found), reverse=True)


def test_captures_filters():
    assert [c.kind for c in ev.captures(EVENTS, kind="queue", tz=TZ)] == ["queue"]
    assert [c.id for c in ev.captures(EVENTS, project="home", tz=TZ)] == ["3QZK1M7-0000C-0006"]
    assert [c.id for c in ev.captures(EVENTS, end=date(2026, 10, 20), tz=TZ)] == ["3QZK1M7-0000C-0007"]
    assert len(ev.captures(EVENTS, start=date(2026, 10, 21), tz=TZ)) == 4


def test_a_device_clock_past_year_9999_falls_back_to_received_at():
    found = ev.captures((item("3QZK1M7-0000C-0005", "note", {"text": "x"}, wall_ms=2**53 - 1),), tz=TZ)
    assert found[0].at == "2026-10-21T15:00:00-07:00"


def test_queue_rows_is_the_queue_fold():
    events = (
        {"ev": "queue.added", "qid": "q1", "project": "cpa", "text": "a", "intent": "do", "origin": "cli"},
        {"ev": "queue.added", "qid": "q2", "project": "home", "text": "b", "intent": "do", "origin": "device"},
        {"ev": "queue.added", "qid": "q3", "project": "cpa", "text": "c", "intent": "do", "origin": "cli"},
        {"ev": "queue.done", "qid": "q1"},
    )
    assert [i.qid for i in ev.queue_rows(events)] == ["q3", "q2"]
    assert [i.qid for i in ev.queue_rows(events, "cpa")] == ["q3"]


def test_hidden_folds_hide_and_unhide_tolerantly():
    events = (
        {"ev": "rec.hidden", "rec": "3QZK1M7-00042-0A1C"},
        {"ev": "rec.hidden", "rec": "3QZK1M7-00042-0A1F"},
        {"ev": "rec.unhidden", "rec": "3qzk1m7-00042-0a1c"},
        {"ev": "rec.hidden", "rec": 5},
        {"ev": "rec.hidden", "rec": "../etc"},
        {"ev": "rec.hidden"},
    )
    assert ev.hidden(events) == frozenset({"3qzk1m7-00042-0a1f"})
    assert ev.hidden(()) == frozenset()
