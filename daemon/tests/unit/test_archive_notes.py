"""Daily notes in the archive (docs/15 §2.1, §3.4): days, pages of a note, checkbox tasks."""

from __future__ import annotations

from datetime import date

import pytest

from cpad.archive import notes
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.notes import DayInfo, Task
from tests.support import archive_tree as at


@pytest.fixture
def root(tmp_path):
    return Root.at(at.build(tmp_path / "notes"))


def test_list_days_unions_notes_and_recording_days_newest_first(root):
    assert notes.list_days(root) == (
        DayInfo("2026-10-21", len(at.NOTE_21.encode()), 4),
        DayInfo("2026-10-20", len(at.NOTE_20.encode()), 2),
    )


def test_list_days_filters_by_range_and_skips_hidden_recordings(root):
    assert notes.list_days(root, start=date(2026, 10, 21)) == (DayInfo("2026-10-21", len(at.NOTE_21.encode()), 4),)
    assert notes.list_days(root, end=date(2026, 10, 20))[0].date == "2026-10-20"
    hidden = frozenset({at.MEMO.lower()})
    assert notes.list_days(root, start=date(2026, 10, 21), hidden=hidden)[0].recordings == 3


def test_a_day_with_recordings_only_has_zero_bytes(root):
    (root.path / "2026-10-20.md").unlink()
    assert notes.list_days(root)[1] == DayInfo("2026-10-20", 0, 2)


def test_list_days_ignores_other_files_and_impossible_dates(root):
    (root.path / "2026-02-30.md").write_text("x")
    (root.path / "recordings" / "notes").mkdir()
    assert [d.date for d in notes.list_days(root)] == ["2026-10-21", "2026-10-20"]


def test_list_days_of_an_empty_archive(tmp_path):
    assert notes.list_days(Root.at(tmp_path / "missing")) == ()


def test_read_day_whole_note(root):
    page = notes.read_day(root, date(2026, 10, 21))
    assert page.date == "2026-10-21"
    assert page.text == at.NOTE_21.rstrip("\n")
    assert (page.total_lines, page.next_offset, page.truncated) == (3, None, False)


def test_read_day_pages_by_line_within_the_budget(root):
    lines = at.NOTE_21.splitlines()
    first = notes.read_day(root, date(2026, 10, 21), budget=len(lines[0].encode()) + 1)
    assert (first.text, first.next_offset, first.truncated) == (lines[0], 1, True)
    rest = notes.read_day(root, date(2026, 10, 21), offset=first.next_offset)
    assert rest.text == "\n".join(lines[1:])
    assert rest.next_offset is None
    assert notes.read_day(root, date(2026, 10, 21), offset=99).text == ""


def test_read_day_cuts_a_line_longer_than_the_budget(root):
    page = notes.read_day(root, date(2026, 10, 21), budget=10)
    assert len(page.text.encode()) <= 10
    assert (page.next_offset, page.truncated) == (1, True)


def test_read_day_missing_note(root):
    with pytest.raises(ArchiveError) as exc:
        notes.read_day(root, date(2026, 1, 1))
    assert exc.value.code == "E_NOT_FOUND"


def test_parse_tasks_daily_note_lines():
    assert notes.parse_tasks(at.NOTE_20) == (
        Task("10:00 #cpa 给 Bob 回电话", False, "cpa", "cpa-3qzk1m7-0000c-0006", 2),
        Task("10:30 修好打印机", True, None, "cpa-3qzk1m7-0000c-0007", 3),
    )


def test_parse_tasks_summary_lines():
    tasks = notes.parse_tasks(at.MEETING_SUMMARY)
    assert tasks[0] == Task("给 Alice 发报价单（我，周五）[@06:01](transcript.md#^t000601)", False, None, "act-1", 10)
    assert tasks[1].done and tasks[1].block == "act-2"


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        ("  * [X] nested", Task("nested", True, None, None, 1)),
        ("+ [ ] plus #home", Task("plus #home", False, "home", None, 1)),
        ("- [ ] #Bad-Tag x", Task("#Bad-Tag x", False, None, None, 1)),
        ("- [-] cancelled", None),
        ("- [ ]", None),
        ("-[ ] no space", None),
        ("text - [ ] inline", None),
    ],
)
def test_parse_tasks_edge_cases(line, expected):
    assert notes.parse_tasks(line) == ((expected,) if expected else ())
