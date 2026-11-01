"""cpa audit rows (docs/14 §8.2): dest.* events only, filters, and never the text."""

from __future__ import annotations

import json
from dataclasses import asdict
from datetime import UTC, datetime

import pytest

from cpad.dest import records as r
from cpad.dest.audit import AuditRow, audit_rows, format_rows, parse_since
from cpad.dest.model import AppOpts, Capture, Decision, Delivery, Dest, FileOpts

TEXT = "周五前给 Alice 发报价 secret-token-123"
ITEM = "3QZK1M7-0000C-0005"
NOW = datetime(2026, 10, 22, 12, 0, tzinfo=UTC).timestamp()
SB = Dest("file.sb", "file", "reading-list", 7, key="o", leaves_mac=True, optin="2026-10-22T09:00:00+00:00",
          file=FileOpts("~/sb", "inbox.md"))  # fmt: skip
CAP = Capture(ITEM, "typed", "file.sb", "key", "cpa", TEXT, "2026-10-22 09:14")
NOTES = Dest("app.notes", "app", "Notes", 9, leaves_mac=True, optin="2026-10-22T09:00:00+00:00",
             app=AppOpts("invalid.example.notes"))  # fmt: skip


def at(hour: int, day: int = 22) -> str:
    return datetime(2026, 10, day, hour, 0, tzinfo=UTC).isoformat(timespec="milliseconds")


def events() -> list[dict]:
    clip = Dest("clip", "clip", "剪贴板", 5, key="c")
    return [
        {"ev": "item.received", "at": at(9), "id": ITEM, "kind": "note", "body": {"text": TEXT, "dest": "file.sb"}},
        r.dest_decided(CAP, Decision("deliver", "file.sb", "archive"), "queued", at(9)),
        r.dest_delivered(Delivery("k", CAP, SB), at=at(9), ref="^cpa-x"),
        r.dest_handed(Delivery("k", CAP, NOTES, by="cli"), at=at(9), via="clip", text="[h] " + TEXT, ref=None),
        r.dest_failed(Delivery("k", CAP, clip, 2), code="E_DEST_TIMEOUT", retryable=False, at=at(10)),
        r.dest_dropped(ITEM, "clip", by="device", at=at(10)),
        r.dest_expired(ITEM, "clip", at=at(11)),
        r.dest_changed("add", SB, by="cli", at=at(8)),
        r.dest_changed("bind", clip, by="cli", at=at(8)),
        {"ev": "dest.downgraded", "at": at(9, 15), "item": ITEM, "dest": "clip"},
        {"ev": "queue.added", "at": at(9), "text": TEXT},
        {"ev": "dest.decided", "at": "not a time", "item": ITEM},
    ]


def test_only_dest_events_are_listed():
    rows = audit_rows(events())
    assert [row.ev for row in rows] == ["dest.downgraded", "dest.changed", "dest.changed", "dest.decided",
                                        "dest.delivered", "dest.handed", "dest.failed", "dest.dropped",
                                        "dest.expired"]  # fmt: skip


def test_rows_are_oldest_first_and_carry_digests():
    rows = audit_rows(events(), since=at(0))
    delivered = next(row for row in rows if row.ev == "dest.delivered")
    assert (delivered.bytes, delivered.sha8) == r.digest(TEXT)
    assert delivered.leaves_mac and delivered.by == "auto" and delivered.dest == "file.sb"
    assert [row.at for row in rows] == sorted(row.at for row in rows)


def test_the_text_never_appears():
    rows = audit_rows(events())
    out = "\n".join(format_rows(rows)) + json.dumps([asdict(row) for row in rows], ensure_ascii=False)
    assert TEXT not in out and "secret-token" not in out and "周五" not in out


def test_since_filters():
    assert {row.ev for row in audit_rows(events(), since=at(10))} == {"dest.failed", "dest.dropped", "dest.expired"}


def test_leaves_mac_filter_lists_what_left_and_what_was_opted_in():
    rows = audit_rows(events(), leaves_mac=True)
    assert [(row.ev, row.dest) for row in rows] == [("dest.changed", "file.sb"), ("dest.delivered", "file.sb"),
                                                    ("dest.handed", "app.notes")]  # fmt: skip
    handed = rows[-1]
    assert handed.by == "cli" and (handed.bytes, handed.sha8) == r.digest("[h] " + TEXT)
    assert "via clip" in handed.detail


def test_dest_filter():
    assert {row.ev for row in audit_rows(events(), dest="clip")} == {"dest.changed", "dest.failed", "dest.dropped",
                                                                    "dest.expired", "dest.downgraded"}  # fmt: skip


def test_details():
    rows = {row.ev: row for row in audit_rows(events(), dest="clip")}
    assert "E_DEST_TIMEOUT" in rows["dest.failed"].detail and "attempt 2" in rows["dest.failed"].detail
    assert rows["dest.downgraded"].detail.startswith("downgrade")
    assert "key=c" in rows["dest.changed"].detail


@pytest.mark.parametrize(
    ("text", "expected"),
    [("7d", datetime(2026, 10, 15, 12, tzinfo=UTC)), ("24h", datetime(2026, 10, 21, 12, tzinfo=UTC)),
     ("30m", datetime(2026, 10, 22, 11, 30, tzinfo=UTC)), ("2026-10-20", None),
     ("2026-10-20T08:00:00+00:00", datetime(2026, 10, 20, 8, tzinfo=UTC))],
)  # fmt: skip
def test_parse_since(text, expected):
    got = datetime.fromisoformat(parse_since(text, NOW))
    if expected is not None:
        assert got == expected
    assert got.tzinfo is not None


@pytest.mark.parametrize("text", ["", "7", "d7", "-1d", "yesterday", "7w"])
def test_parse_since_refuses(text):
    with pytest.raises(ValueError):
        parse_since(text, NOW)


def test_format_rows():
    (line,) = format_rows([AuditRow(at(9), "dest.delivered", ITEM, "file.sb", "ok", "auto", 26, "abcd1234", True)])
    assert "dest.delivered" in line and "file.sb" in line and ITEM in line
    assert "bytes=26" in line and "sha8=abcd1234" in line and "leaves-mac" in line and "by=auto" in line
    (bare,) = format_rows([AuditRow(at(9), "dest.expired", None, None, "expired")])
    assert " - " in bare and "bytes" not in bare
