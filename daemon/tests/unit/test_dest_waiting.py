"""Waiting to be sent (docs/25 §5.4, §3.4): the rows waiting for a send from this Mac, the capture a Mac send hands
over, the drafts other processes filled, and how a Mac send is refused. Pure."""

from __future__ import annotations

from datetime import datetime

import pytest

from cpad.dest.catalog import defaults, put, with_usual
from cpad.dest.model import Capture, Decision, Dest, UrlOpts
from cpad.dest.waiting import (
    E_BUSY,
    E_DEST_OFF,
    E_NEEDS_DEVICE,
    E_OTHER_PROFILE,
    E_TOO_LONG,
    E_UNKNOWN_DEST,
    SendError,
    memo_capture,
    recent_drafts,
    refusal,
    waiting,
)
from cpad.filing import fold as ff
from cpad.filing.args import E_BAD_ARG, E_NO_TEXT, E_UNKNOWN_ID
from cpad.projects.registry import Project
from cpad.recordings import records as r

DEV = "020000000001"
AT = "2026-10-22T09:00:00.000+00:00"
NOW = datetime.fromisoformat("2026-10-22T12:00:00+00:00").timestamp()
JUST_NOW = "2026-10-22T11:59:55.000+00:00"  # a Mac send running this long is still running
MEMO, OTHER, MEET, NOTE = "3QZK1M7-0000A-0001", "3QZK1M7-0000A-0002", "3QZK1M7-0000A-0003", "3QZK1M7-0000C-0005"
KNOWN = ff.Known.of((Project("acmeshop", "Acme", 3, "/b"), Project("cpa", "Cardputer", 1, "/c")))
GROK = Dest("url.grok", "url", "Grok", 8, key="x", leaves_mac=True, optin=AT,
            url=UrlOpts("test-scheme://p?text={text}", "invalid.example.chat", None, effect="draft",
                        verified="draft"))  # fmt: skip
CAT = with_usual(put(defaults(), GROK), "acmeshop", "url.grok")


def memo(rec: str = MEMO, project: str | None = "acmeshop", text: str | None = "给 Grok 的话", mode: str = "memo",
         at: str = AT, **extra) -> list[dict]:  # fmt: skip
    session = {**r.archived(dev=DEV, rec=rec, dir=f"recordings/2026-10-22/0900-{rec.lower()}", mode=mode,
                            project=project, segments=1, dur_ms=12000, started_ms=1792659600000, uncertain=False,
                            gaps=[], consent=None, at=at), **extra}  # fmt: skip
    done = r.result(rec=rec, state="done", level="summary", title=None, memo_text=text, memo_intent=None, actions_n=0,
                    error_code=None, session=session, at=at)  # fmt: skip
    return [session, done]


def decided(item: str, action: str, dest: str = "url.grok", origin: str = "queued", at: str = AT, **extra) -> dict:
    return {"ev": "dest.decided", "at": at, "item": item, "src": "voice", "want": dest, "dest": dest,
            "kind": dest.partition(".")[0], "action": action, "reason": "await_device", "how": "key",
            "origin": origin, "prof": None, **extra}  # fmt: skip


def rows(events, cat=CAT, expired: bool = False, now: float = NOW) -> list[tuple]:
    return [(w.ref, w.why, w.dest, w.st, w.project) for w in waiting(events, cat, KNOWN, now, expired=expired)]


# --- the capture of a Mac send ---------------------------------------------------------------------


def test_a_memo_is_handed_over_with_its_memo_text_project_and_profile():
    cap = memo_capture(memo(prof="home"), KNOWN, MEMO.lower(), "url.grok")
    assert cap == Capture(MEMO, "voice", "url.grok", "pick", "acmeshop", "给 Grok 的话", cap.stamp, False, "home",
                          12000)  # fmt: skip


def test_never_the_transcript_and_meetings_say_so():
    assert memo_capture(memo(text=None), KNOWN, MEMO, "url.grok").text is None
    assert memo_capture(memo(mode="meeting"), KNOWN, MEMO, "url.grok").meeting
    edited = [*memo(), r.edited_project(rec=MEMO, project="cpa", at=AT, by="cli")]
    assert memo_capture(edited, KNOWN, MEMO, "url.grok").project == "cpa"


@pytest.mark.parametrize(("rec", "code"), [("nope", E_BAD_ARG), (OTHER, E_UNKNOWN_ID)])
def test_an_id_that_is_no_archived_recording(rec, code):
    with pytest.raises(SendError) as err:
        memo_capture(memo(), KNOWN, rec, "url.grok")
    assert (err.value.code, err.value.field) == (code, "id")


# --- refusals --------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("decision", "code", "field"),
    [(Decision("downgrade", "daily", "meeting"), E_BAD_ARG, "id"),
     (Decision("downgrade", "daily", "unknown"), E_UNKNOWN_DEST, "dest"),
     (Decision("downgrade", "daily", "disabled"), E_DEST_OFF, "dest"),
     (Decision("downgrade", "daily", "no_optin"), E_DEST_OFF, "dest"),
     (Decision("downgrade", "daily", "unavailable"), E_DEST_OFF, "dest"),
     (Decision("downgrade", "daily", "other_profile"), E_OTHER_PROFILE, "dest"),
     (Decision("downgrade", "daily", "no_text"), E_NO_TEXT, "text"),
     (Decision("downgrade", "daily", "too_long"), E_TOO_LONG, "text"),
     (Decision("downgrade", "daily", "no_project"), E_BAD_ARG, "dest"),
     (Decision("hold", "url.grok", "needs_device"), E_NEEDS_DEVICE, "dest")],
)  # fmt: skip
def test_every_way_decide_refuses_a_mac_send_has_a_code(decision, code, field):
    found = refusal(decision)
    assert (found.code, found.field) == (code, field)


def test_a_delivery_is_no_refusal_and_codes_exit_like_filing():
    assert refusal(Decision("deliver", "url.grok", "mac")) is None
    assert SendError(E_UNKNOWN_DEST, "dest").exit_code == 2 and SendError(E_BUSY, "ref").exit_code == 1
    assert SendError(E_NEEDS_DEVICE, "dest").doc() == {"ok": False, "v": 1, "error": E_NEEDS_DEVICE, "field": "dest"}


# --- the list ---------------------------------------------------------------------------------------


def test_a_project_with_a_usual_destination_suggests_its_new_memos():
    events = [*memo(), *memo(OTHER, project="cpa"), *memo(MEET, mode="meeting")]
    assert rows(events) == [(MEMO, "usual", "url.grok", None, "acmeshop")]
    (row,) = waiting(events, CAT, KNOWN, NOW)
    assert (row.text, row.src, row.at, row.code) == ("给 Grok 的话", "voice", AT, None)


@pytest.mark.parametrize(
    "extra",
    [[decided(MEMO, "deliver", origin="mac", at=JUST_NOW)],
     [{"ev": "dest.dropped", "at": AT, "item": MEMO, "dest": "url.grok"}], [r.hidden(rec=MEMO, at=AT)]],
)  # fmt: skip
def test_a_memo_once_decided_dropped_or_hidden_is_not_suggested(extra):
    assert rows([*memo(), *extra]) == []


def test_no_suggestion_without_text_after_a_day_with_a_dest_of_its_own_or_while_the_dest_is_off():
    assert rows(memo(text=" ")) == []
    assert rows(memo(at="2026-10-21T11:59:00.000+00:00")) == []
    assert rows(memo(dest="todo")) == []
    off = put(CAT, Dest("url.grok", "url", "Grok", 8, key="x", leaves_mac=True, optin=None, url=GROK.url))
    assert rows(memo(), cat=off) == []


def test_held_and_failed_items_come_first_with_their_state_and_code():
    events = [*memo(), *memo(OTHER, project="cpa", dest="url.grok"), decided(OTHER, "hold"),
              decided(MEMO, "deliver", origin="mac", attempt=1),
              {"ev": "dest.failed", "at": AT, "item": MEMO, "dest": "url.grok", "kind": "url", "code": "E_TOO_LONG",
               "retryable": False, "attempt": 1}]  # fmt: skip
    found = waiting(events, CAT, KNOWN, NOW)
    assert [(w.ref, w.why, w.st, w.code, w.project, w.text) for w in found] == [
        (OTHER, "held", "hold", None, "cpa", "给 Grok 的话"), (MEMO, "held", "failed", "E_TOO_LONG", "acmeshop",
                                                              "给 Grok 的话")]  # fmt: skip


def test_a_typed_note_held_for_the_device_shows_its_own_text():
    body = {"text": "打的\x1b字", "project": "cpa", "dest": "url.grok"}
    note = {"ev": "item.received", "at": AT, "dev": DEV, "id": NOTE, "kind": "note", "body": body,
            "at_dev": {"up_ms": 5, "wall_ms": None}, "received_at": AT}  # fmt: skip
    (row,) = waiting([note, decided(NOTE, "hold") | {"src": "typed"}], CAT, KNOWN, NOW)
    assert (row.ref, row.src, row.text, row.project) == (NOTE, "typed", "打的 字", "cpa")


def test_expired_rows_only_when_asked_and_only_for_a_week():
    events = [*memo(), decided(MEMO, "hold"), {"ev": "dest.expired", "at": AT, "item": MEMO, "dest": "url.grok"}]
    assert rows(events) == []
    assert rows(events, expired=True) == [(MEMO, "expired", "url.grok", None, "acmeshop")]
    assert rows(events, expired=True, now=NOW + 8 * 86_400) == []


# --- drafts other processes filled -----------------------------------------------------------------


def test_recent_drafts_come_from_links_handed_within_the_hold():
    recent = "2026-10-22T11:50:00.000+00:00"
    handed = {"ev": "dest.handed", "at": recent, "item": MEMO, "dest": "url.grok", "via": "url", "to": "abcd1234"}
    old = handed | {"at": "2026-10-22T11:00:00.000+00:00", "to": "00000000"}
    clip = handed | {"via": "clip", "to": "ffffffff"}
    bad = handed | {"at": "nope", "to": "11111111"}
    found = recent_drafts([handed, old, clip, bad, {"ev": "dest.handed", "dest": "url.grok", "via": "url"}], NOW)
    assert found == {("url.grok", "abcd1234"): datetime.fromisoformat(recent).timestamp()}


def test_the_one_time_test_stops_holding_its_draft_once_answered():
    """The test says to delete its marked text from the input box; after verify, the first real send is a link again."""
    test = {"ev": "dest.handed", "at": "2026-10-22T11:50:00.000+00:00", "item": "test-1234", "dest": "url.grok",
            "via": "url", "to": "abcd1234"}  # fmt: skip
    verified = {"ev": "dest.changed", "at": "2026-10-22T11:51:00.000+00:00", "op": "verify", "id": "url.grok"}
    other = verified | {"id": "url.other"}
    assert ("url.grok", "abcd1234") in recent_drafts([test, other], NOW)
    assert recent_drafts([test, verified], NOW) == {}
    assert ("url.grok", "abcd1234") in recent_drafts([verified, test], NOW)
