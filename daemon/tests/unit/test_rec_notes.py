"""NotesWriter with recording lines (docs/11 §6.7): idempotent by block id, replayed by backfill."""

from __future__ import annotations

from datetime import timedelta, timezone

import pytest

from cpad.events import records as ev
from cpad.notes.writer import NotesWriter
from cpad.protocol import messages as m
from cpad.recordings import records as r

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

TZ = timezone(timedelta(hours=8))
SID = "K3M9Q2T-00042-0A1C"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
AT = "2026-10-21T06:49:02.000+00:00"
SESSION = {"dir": DIR, "mode": "meeting", "project": "cpa", "started_ms": 1792562520000, "uncertain": False,
           "dur_ms": 2520000}  # fmt: skip


def result(state: str, **over) -> dict:
    fields = {"level": "summary" if state == "done" else None, "title": "同步", "memo_text": None, "memo_intent": None,
              "actions_n": 1, "error_code": None if state == "done" else "E_STT"}  # fmt: skip
    return r.result(rec=SID, state=state, session=SESSION, at=AT, **{**fields, **over})


def note_lines(tmp_path) -> list[str]:
    path = tmp_path / "2026-10-21.md"
    return path.read_text(encoding="utf-8").splitlines() if path.exists() else []


def test_result_lines_are_written_once_per_block_id(tmp_path):
    writer = NotesWriter(tmp_path, TZ)
    assert writer.write(result("done"))
    assert not writer.write(result("done"))
    assert note_lines(tmp_path) == [f"- 14:02 #cpa 会议：[同步]({DIR}/summary.md) 42 分钟 · 待办 1 ^cpa-{SID.lower()}"]


def test_failed_then_done_after_a_rerun_writes_both_lines(tmp_path):
    writer = NotesWriter(tmp_path, TZ)
    assert writer.write(r.stale(rec=SID, session=SESSION, at=AT))
    assert not writer.write(result("failed"))  # same -u block id as the stale line
    assert writer.write(result("done"))  # ^cpa-<sid> is not hidden by ^cpa-<sid>-u
    lines = note_lines(tmp_path)
    assert len(lines) == 2 and lines[0].endswith(f"^cpa-{SID.lower()}-u") and lines[1].endswith(f"^cpa-{SID.lower()}")


def test_a_done_line_does_not_hide_a_later_failure_line(tmp_path):
    writer = NotesWriter(tmp_path, TZ)
    assert writer.write(result("done"))
    assert writer.write(result("failed"))


def test_injected_block_ids_inside_text_do_not_count(tmp_path):
    writer = NotesWriter(tmp_path, TZ)
    memo = r.result(rec="K3M9Q2T-00042-0A1D", state="done", level="summary", title=None,
                    memo_text=f"say ^cpa-{SID.lower()} please", memo_intent="note", actions_n=0, error_code=None,
                    session={**SESSION, "mode": "memo"}, at=AT)  # fmt: skip
    assert writer.write(memo)
    assert writer.write(result("done"))


def test_backfill_replays_rec_events_with_notes_and_skips_the_rest(tmp_path):
    item = m.OutboxItem(id="3QZK1M7-0000C-0001", kind="note", at=m.At(1, 1792562520000),
                        body=m.NoteBody(text="idea", project="cpa"))  # fmt: skip
    history = [
        ev.item_received("020000000001", item, AT, orphan=False),
        r.archived(
            dev="020000000001",
            rec=SID,
            dir=DIR,
            mode="meeting",
            project="cpa",
            segments=1,
            dur_ms=1,
            started_ms=1,
            uncertain=False,
            gaps=[],
            consent=None,
            at=AT,
        ),  # fmt: skip
        result("failed"),
        result("done"),
        {"ev": "rec.result", "rec": SID},
    ]
    writer = NotesWriter(tmp_path, TZ)
    assert writer.backfill(history) == 3
    assert writer.backfill(history) == 0
    assert len(note_lines(tmp_path)) == 3


def test_recording_records_never_add_queue_items():
    built = [result("done"), result("failed"), r.stale(rec=SID, session=SESSION, at=AT), r.seg_deleted("d", SID, AT),
             r.audio_swept(SID, 2, AT)]  # fmt: skip
    assert all(rec["ev"].startswith("rec.") for rec in built)
