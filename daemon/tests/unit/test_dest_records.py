"""Captures from event records (docs/14 §5.1, §6.3) and the dest.* audit records (§8.1): never the text."""

from __future__ import annotations

import hashlib
import json
from dataclasses import replace
from datetime import UTC

import pytest

from cpad.dest import records as r
from cpad.dest.capture import capture_from_item, capture_from_result, stamp
from cpad.dest.model import VOICE_TEXT_MAX, Capture, Decision, Delivery, Dest, FileOpts, UrlOpts
from cpad.logging.redact import redact_text

ITEM = "3QZK1M7-0000C-0005"
REC = "K3M9Q2T-00042-0A1C"
WALL = 1792660443000  # 2026-10-22T09:14:03Z
TEXT = "周五前给 Alice 发报价"


def received(body: dict, **extra) -> dict:
    return {"ev": "item.received", "at": "2026-10-22T09:14:05.000+00:00", "dev": "020000000001", "id": ITEM,
            "kind": "note", "body": {"text": TEXT, "project": "cpa", **body}, "at_dev": {"up_ms": 5, "wall_ms": WALL},
            "received_at": "2026-10-22T09:14:05.000+00:00", **extra}  # fmt: skip


def result(**extra) -> dict:
    return {"ev": "rec.result", "at": "2026-10-22T10:00:00.000+00:00", "rec": REC, "state": "done", "level": "summary",
            "title": "t", "memo_text": "买 USB-C hub", "memo_intent": None, "actions_n": 0, "error_code": None,
            "dir": "2026/10/22/x", "mode": "memo", "project": None, "started_ms": WALL, "uncertain": False,
            "dur_ms": 9000, "dest": "file.sb", **extra}  # fmt: skip


# --- stamp / captures ------------------------------------------------------------------------


def test_stamp_uses_the_device_clock_or_marks_the_receive_time():
    assert stamp(WALL, "2026-10-22T09:14:05.000+00:00", UTC) == "2026-10-22 09:14"
    assert stamp(None, "2026-10-22T09:20:05.000+00:00", UTC) == "~2026-10-22 09:20"
    assert stamp(2**53 - 1, "2026-10-22T09:20:05.000+00:00", UTC) == "~2026-10-22 09:20"


def test_capture_from_an_outbox_note():
    cap = capture_from_item(received({"dest": "clip"}), UTC)
    assert cap == Capture(ITEM, "typed", "clip", "key", "cpa", TEXT, "2026-10-22 09:14")


@pytest.mark.parametrize(("how", "expected"), [(None, "key"), ("key", "key"), ("pick", "pick"), ("app", "app"),
                                               ("later", "app"), (3, "app")])  # fmt: skip
def test_how_defaults_to_key_and_unknown_values_are_the_strictest(how, expected):
    assert capture_from_item(received({"dest": "clip", "how": how}), UTC).how == expected


def test_orphan_note_has_no_project():
    assert capture_from_item(received({"dest": "queue"}, orphan=True), UTC).project is None


@pytest.mark.parametrize(
    "record",
    [received({}), received({"dest": "daily"}), received({"dest": None}), received({"dest": 7}),
     {**received({"dest": "clip"}), "kind": "queue"}, {**received({"dest": "clip"}), "ev": "queue.added"}, {}],
)  # fmt: skip
def test_no_capture_without_a_non_daily_dest(record):
    assert capture_from_item(record, UTC) is None


def test_capture_from_a_memo_result():
    cap = capture_from_result(result(how="pick", project="cpa"), UTC)
    assert cap == Capture(REC, "voice", "file.sb", "pick", "cpa", "买 USB-C hub", "2026-10-22 09:14", dur_ms=9000)


def test_captures_carry_the_profile_they_were_made_in():
    assert capture_from_result(result(prof="work"), UTC).prof == "work"
    assert capture_from_item(received({"dest": "clip"}, prof="home"), UTC).prof == "home"
    assert capture_from_item(received({"dest": "clip"}), UTC).prof is None
    assert capture_from_result(result(prof=5, dur_ms="x"), UTC) == capture_from_result(result(dur_ms=None), UTC)
    assert capture_from_result(result(uncertain=True), UTC).stamp == "~2026-10-22 09:14"


def test_memo_text_is_capped_and_a_transcript_only_memo_has_no_text():
    assert len(capture_from_result(result(memo_text="x" * 3000), UTC).text) == VOICE_TEXT_MAX
    assert capture_from_result(result(level="transcript", memo_text=None), UTC).text is None


@pytest.mark.parametrize(
    "record",
    [result(mode="meeting"), result(state="failed"), result(dest="daily"), result(dest=None),
     {k: v for k, v in result().items() if k != "dest"}, {**result(), "ev": "rec.stale"}],
)  # fmt: skip
def test_no_capture_for_meetings_failures_or_daily(record):
    assert capture_from_result(record, UTC) is None


@pytest.mark.parametrize(
    "record",
    [{**received({"dest": "clip"}), "received_at": "not a time", "at_dev": {"up_ms": 1, "wall_ms": None}},
     {**received({"dest": "clip"}), "id": None},
     {k: v for k, v in received({"dest": "clip"}).items() if k != "id"}, {**received({}), "body": ["clip"]},
     {**received({"dest": "clip"}), "at_dev": "x"}],
)  # fmt: skip
def test_malformed_items_are_not_captures(record):
    assert capture_from_item(record, UTC) is None


@pytest.mark.parametrize("record", [result(at="soon", started_ms=None), result(rec=5), result(started_ms="x")])
def test_malformed_results_are_not_captures(record):
    if record["started_ms"] == "x":
        assert capture_from_result(record, UTC).stamp.startswith("~")
    else:
        assert capture_from_result(record, UTC) is None


# --- audit records ---------------------------------------------------------------------------

CAP = Capture(ITEM, "typed", "file.sb", "key", "cpa", TEXT, "2026-10-22 09:14")
SB = Dest("file.sb", "file", "reading-list", 7, key="o", leaves_mac=True, optin="2026-10-22T09:00:00+00:00",
          file=FileOpts("~/sb", "inbox.md"))  # fmt: skip
AT = "2026-10-22T09:14:06.000+00:00"


def test_digest_matches_the_log_redaction():
    size, sha8 = r.digest(TEXT)
    assert size == len(TEXT.encode()) and sha8 == hashlib.sha256(TEXT.encode()).hexdigest()[:8]
    assert redact_text(TEXT) == f"<text len={size} sha8={sha8}>"


def test_dest_decided():
    rec = r.dest_decided(CAP, Decision("queue", "queue", "queue", "todo", True), "queued", AT)
    assert rec == {"ev": "dest.decided", "at": AT, "item": ITEM, "src": "typed", "want": "file.sb", "dest": "queue",
                   "kind": "queue", "action": "queue", "reason": "queue", "how": "key", "origin": "queued",
                   "prof": None, "intent": "todo"}  # fmt: skip
    plain = r.dest_decided(CAP, Decision("downgrade", "daily", "unknown"), "live", AT)
    assert "intent" not in plain and plain["kind"] == "daily"


def test_dest_delivered_and_failed():
    d = Delivery("k", replace(CAP, prof="home"), SB, attempt=2, by="device")
    size, sha8 = r.digest(TEXT)
    assert r.dest_delivered(d, at=AT, ref="^cpa-x") == {
        "ev": "dest.delivered", "at": AT, "item": ITEM, "dest": "file.sb", "kind": "file", "by": "device",
        "bytes": size, "sha8": sha8, "ref": "^cpa-x", "leaves_mac": True, "attempt": 2, "prof": "home",
    }  # fmt: skip
    assert r.dest_failed(d, code="E_DEST_IO", retryable=True, at=AT) == {
        "ev": "dest.failed", "at": AT, "item": ITEM, "dest": "file.sb", "kind": "file", "code": "E_DEST_IO",
        "retryable": True, "attempt": 2, "prof": "home",
    }  # fmt: skip


def test_dest_handed_says_handed_never_delivered_and_carries_no_text():
    grok = Dest("url.g", "url", "G", 8, leaves_mac=True, optin=AT, url=UrlOpts("test-scheme://a?q={text}", "b.c", None))
    d = Delivery("k", replace(CAP, prof="home"), grok, by="cli")
    size, sha8 = r.digest("[Cardputer 打字·#cpa] " + TEXT)
    assert r.dest_handed(d, at=AT, via="url", text="[Cardputer 打字·#cpa] " + TEXT, ref=None) == {
        "ev": "dest.handed", "at": AT, "item": ITEM, "dest": "url.g", "kind": "url", "via": "url", "by": "cli",
        "bytes": size, "sha8": sha8, "ref": None, "leaves_mac": True, "attempt": 1, "prof": "home",
        "to": r.digest("")[1],
    }  # fmt: skip
    clip = r.dest_handed(d, at=AT, via="clip", text="x", ref="too_long")
    assert "to" not in clip  # only a filled draft holds its target (docs/25 §3.6)


def test_dropped_expired_changed():
    dropped = {"ev": "dest.dropped", "at": AT, "item": ITEM, "dest": "clip", "by": "device", "prof": None}
    assert r.dest_dropped(ITEM, "clip", by="device", at=AT) == dropped
    assert r.dest_expired(ITEM, "clip", at=AT, prof="work") == {"ev": "dest.expired", "at": AT, "item": ITEM,
                                                                "dest": "clip", "prof": "work"}  # fmt: skip
    assert r.dest_changed("add", SB, by="cli", at=AT) == {
        "ev": "dest.changed", "at": AT, "op": "add", "id": "file.sb", "by": "cli", "enabled": True, "key": "o",
        "leaves_mac": True, "optin": "2026-10-22T09:00:00+00:00", "fl": "c", "profiles": [],
    }  # fmt: skip


def test_dest_changed_of_a_url_records_its_handler_and_verification_never_its_template():
    url = UrlOpts("test-scheme://a/b?text={text}", "invalid.example.chat", "ABCDE12345", "draft", "draft",
                  map=(("*", "bot1"),))  # fmt: skip
    chat = Dest("url.chat", "url", "Chat", 8, leaves_mac=True, optin=AT, url=url, profiles=("home",))
    rec = r.dest_changed("verify", chat, by="cli", at=AT)
    assert (rec["fl"], rec["verified"], rec["profiles"]) == ("lc", "draft", ["home"])
    assert rec["handler"] == {"bundle": "invalid.example.chat", "team": "ABCDE12345"}
    assert "test-scheme" not in json.dumps(rec) and "bot1" not in json.dumps(rec)


def test_no_record_carries_the_text():
    d = Delivery("k", CAP, SB)
    recs = [r.dest_decided(CAP, Decision("deliver", "file.sb", "archive"), "queued", AT), r.dest_delivered(d, at=AT),
            r.dest_failed(d, code="E_DEST_IO", retryable=True, at=AT)]  # fmt: skip
    assert all(TEXT not in json.dumps(rec, ensure_ascii=False) for rec in recs)


def test_dest_events():
    expected = {"dest.decided", "dest.delivered", "dest.handed", "dest.failed", "dest.dropped", "dest.expired",
                "dest.changed", "dest.downgraded"}  # fmt: skip
    assert expected == r.DEST_EVENTS
