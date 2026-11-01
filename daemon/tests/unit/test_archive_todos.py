"""Todos = checkboxes in daily notes and recording summaries (docs/15 §3.4), newest first, paged by key."""

from __future__ import annotations

from datetime import date

import pytest

from cpad.archive import todos
from cpad.archive.fs import Root
from tests.support import archive_tree as at


@pytest.fixture
def root(tmp_path):
    return Root.at(at.build(tmp_path / "notes"))


def _texts(page):
    return [t.text for t in page.items]


def test_open_todos_newest_first(root):
    found = todos.collect(root)
    assert _texts(found) == [
        "给 Alice 发报价单（我，周五）[@06:01](transcript.md#^t000601)",
        "~14:05 待办：周五前给 Alice 发报价单。 [音频](recordings/2026-10-21/1405-3qzk1m7-00042-0a1f/seg-000.wav)",
        "10:00 #cpa 给 Bob 回电话",
    ]
    meeting, memo, bob = found.items
    assert (meeting.source_kind, meeting.source_id, meeting.project, meeting.block) == (
        "recording", at.MEETING, "cpa", "act-1")  # fmt: skip
    assert (memo.source_kind, memo.source_id, memo.project, memo.date) == ("note", "2026-10-21", None, "2026-10-21")
    assert (bob.project, bob.block, bob.done) == ("cpa", "cpa-3qzk1m7-0000c-0006", False)
    assert (found.next_key, found.partial) == (None, False)


def test_status_filters(root):
    assert _texts(todos.collect(root, status="done")) == ["确认交付时间", "10:30 修好打印机"]
    assert len(todos.collect(root, status="all").items) == 5
    with pytest.raises(ValueError):
        todos.collect(root, status="maybe")


def test_summary_tasks_keep_document_order(root):
    found = todos.collect(root, status="all", start=date(2026, 10, 21))
    assert _texts(found)[:2] == ["给 Alice 发报价单（我，周五）[@06:01](transcript.md#^t000601)", "确认交付时间"]


def test_range_project_and_hidden(root):
    assert _texts(todos.collect(root, end=date(2026, 10, 20))) == ["10:00 #cpa 给 Bob 回电话"]
    assert len(todos.collect(root, project="cpa").items) == 2
    hidden = frozenset({at.MEETING.lower()})
    assert at.MEETING not in [t.source_id for t in todos.collect(root, hidden=hidden).items]


def test_pages_by_key(root):
    first = todos.collect(root, status="all", limit=2)
    assert len(first.items) == 2 and first.next_key == first.items[-1].key
    second = todos.collect(root, status="all", after=first.next_key, limit=2)
    third = todos.collect(root, status="all", after=second.next_key, limit=2)
    keys = [t.key for t in (*first.items, *second.items, *third.items)]
    assert keys == [t.key for t in todos.collect(root, status="all").items]
    assert third.next_key is None


def test_the_file_cap_marks_the_list_partial(root, monkeypatch):
    monkeypatch.setattr(todos, "FILES_MAX", 1)
    assert todos.collect(root, status="all").partial


def test_a_capped_list_resumes_at_the_first_day_it_did_not_read(root, monkeypatch):
    every = [t.key for t in todos.collect(root, status="all").items]
    monkeypatch.setattr(todos, "FILES_MAX", 1)
    first = todos.collect(root, status="all", limit=50)
    assert first.partial and first.next_key == "2026-10-20/~"
    assert {t.date for t in first.items} == {"2026-10-21"}
    second = todos.collect(root, status="all", after=first.next_key, limit=50)
    assert not second.partial and second.next_key is None
    assert [t.key for t in (*first.items, *second.items)] == every


def test_unreadable_files_are_skipped(root):
    (root.path / "2026-10-20.md").write_bytes(b"- [ ] x\n" * (2 << 18) + b"- [ ] over the cap\n")
    assert "10:00 #cpa 给 Bob 回电话" not in _texts(todos.collect(root))
