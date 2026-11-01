"""rec_entry in English, the default (cpad.i18n): the line is in the language of the moment it is written; the block id,
the box, the time and the #tag do not depend on it, so a reader (and the notes writer's backfill) works on both."""

from __future__ import annotations

import pytest

from cpad.archive import notes
from cpad.notes.writer import NotesWriter
from cpad.recordings import records as r
from tests.unit.test_rec_route import AT, DIR, SID, TZ, filed, line, result, session


def test_meeting_memo_and_todo_lines_in_english():
    assert line(result()) == (f"- 14:02 #cpa Meeting: [客户同步：定价和交付]({DIR}/summary.md) 42 min · to-dos: 3 "
                              f"^cpa-{SID.lower()}")  # fmt: skip
    note = result(mode="memo", memo_text="move dedupe to sid", memo_intent="note", actions_n=0)
    assert line(note) == f"- 14:02 #cpa Voice: move dedupe to sid [audio]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"
    todo = result(mode="memo", memo_text="send Alice the quote", memo_intent="todo", project=None)
    assert line(todo) == f"- [ ] 14:02 send Alice the quote [audio]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"


def test_long_memo_transcript_only_unprocessed_and_filed_in_english():
    out = line(result(mode="memo", memo_text="First sentence. " + "more " * 100, memo_intent="note"))
    assert out.startswith("- 14:02 #cpa Voice: First sentence. [full text](") and f"({DIR}/summary.md)" in out
    assert line(result(level="transcript", title=None)) == (
        f"- 14:02 #cpa Meeting (transcript only): [transcript]({DIR}/transcript.md) 42 min ^cpa-{SID.lower()}"
    )
    assert line(result(mode="memo", level="transcript", title=None, dest="todo")).startswith(
        "- [ ] 14:02 #cpa Voice (transcript only): [transcript]("
    )
    failed = line(result(state="failed", level=None, error_code="E_STT"))
    assert failed == f"- 14:02 #cpa Meeting (not transcribed) [audio]({DIR}/) ^cpa-{SID.lower()}-u"
    assert line(r.stale(rec=SID, session=session(), at=AT)) == failed
    assert line(filed()) == f"- 14:02 #blog ↪ Filed: 客户同步 ^cpa-{SID.lower()}-f1-blog"
    assert line(filed(mode="memo", title=None)).endswith("↪ Filed: Voice ^cpa-" + SID.lower() + "-f1-blog")
    assert line(result(title=None)).startswith("- 14:02 #cpa Meeting: [Meeting](")


@pytest.mark.parametrize("lang", ["zh", "en"])
def test_a_line_written_in_either_language_is_read_the_same(tmp_path, monkeypatch, lang):
    """Readers parse the box, the #tag and the block id, never the words: lines from before a switch stay readable."""
    monkeypatch.setenv("CPA_LANG", lang)
    writer = NotesWriter(tmp_path, TZ)
    todo = result(mode="memo", memo_text="send Alice the quote", memo_intent="todo")
    assert writer.write(todo) and writer.write(filed())
    (task,) = notes.parse_tasks((tmp_path / "2026-10-21.md").read_text(encoding="utf-8"))
    assert (task.done, task.project, task.block) == (False, "cpa", f"cpa-{SID.lower()}")
    assert task.text.startswith("14:02 #cpa send Alice the quote [")


def test_a_language_switch_never_rewrites_or_repeats_a_line(tmp_path, monkeypatch):
    monkeypatch.setenv("CPA_LANG", "zh")
    writer = NotesWriter(tmp_path, TZ)
    assert writer.write(result())
    before = (tmp_path / "2026-10-21.md").read_text(encoding="utf-8")
    monkeypatch.setenv("CPA_LANG", "en")
    assert writer.backfill([result()]) == 0 and not writer.write(result())
    assert (tmp_path / "2026-10-21.md").read_text(encoding="utf-8") == before and "会议：[" in before
