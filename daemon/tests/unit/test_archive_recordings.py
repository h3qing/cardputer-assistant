"""Recordings in the archive (docs/15 §3.4, recording contract v1): scan, rows, detail, transcript pages.

Files written by the transcriber are untrusted: anything that fails the contract is skipped, never fatal.
"""

from __future__ import annotations

import json
import time
from datetime import date

import pytest

from cpad.archive import recordings as recs
from cpad.archive.fs import ArchiveError, Root
from cpad.archive.recordings import Action, Mark, RecRef, Sentence
from tests.support import archive_tree as at


@pytest.fixture
def root(tmp_path):
    return Root.at(at.build(tmp_path / "notes"))


def _ref(sid: str, root: Root) -> RecRef:
    return recs.find(root, sid.lower())


def test_scan_newest_first_with_range_hidden_and_cap(root):
    found = recs.scan(root)
    assert [r.id for r in found.refs] == [at.PULLING, at.BLOCKED, at.MEMO, at.MEETING, at.FAILED, at.OLD]
    assert not found.partial
    assert found.refs[3] == RecRef("2026-10-21", f"1402-{at.MEETING.lower()}", at.MEETING.lower())
    assert found.refs[3].key == f"2026-10-21/1402-{at.MEETING.lower()}"
    assert found.refs[3].parts == ("recordings", "2026-10-21", f"1402-{at.MEETING.lower()}")
    assert [r.id for r in recs.scan(root, end=date(2026, 10, 20)).refs] == [at.FAILED, at.OLD]
    assert at.MEMO not in [r.id for r in recs.scan(root, hidden=frozenset({at.MEMO.lower()})).refs]
    capped = recs.scan(root, max_dirs=2)
    assert (len(capped.refs), capped.partial) == (2, True)


def test_scan_resumes_below_a_key_and_the_cap_counts_from_there(root):
    every = recs.scan(root).refs
    pages, after = [], None
    while True:
        page = recs.scan(root, max_dirs=2, after=after)
        pages.append(page)
        if not page.partial:
            break
        assert page.last == page.refs[-1].key
        after = page.last
    assert [r for page in pages for r in page.refs] == list(every) and len(pages) == 3
    hidden = recs.scan(root, max_dirs=2, hidden=frozenset({at.BLOCKED.lower()}))
    assert [r.id for r in hidden.refs] == [at.PULLING] and hidden.last == every[1].key


def test_scan_skips_foreign_names(root):
    day = root.path / "recordings" / "2026-10-21"
    (day / "notes").mkdir()
    (day / f"1403-{at.MEETING}").mkdir()  # upper case is not a name cpad writes
    (root.path / "recordings" / "2026-13-01").mkdir()
    assert len(recs.scan(root).refs) == 6


def test_find_is_case_insensitive_and_reports_missing(root):
    assert _ref(at.MEETING, root).sid == at.MEETING.lower()
    with pytest.raises(ArchiveError) as exc:
        recs.find(root, "3qzk1m7-00099-0000")
    assert exc.value.code == "E_NOT_FOUND"


def test_rows_follow_the_contract_files(root):
    rows = {r.id: r for r in (recs.row(root, ref) for ref in recs.scan(root).refs)}
    meeting = rows[at.MEETING]
    assert (meeting.date, meeting.started, meeting.mode, meeting.project) == (
        "2026-10-21", "2026-10-21T15:02:00-07:00", "meeting", "cpa")  # fmt: skip
    assert (meeting.duration_s, meeting.state, meeting.todos_n) == (675, "done", 1)
    assert meeting.title == "客户同步：定价和交付"
    assert meeting.has_summary and meeting.has_transcript
    assert {k: r.state for k, r in rows.items()} == {
        at.MEETING: "done", at.MEMO: "done", at.BLOCKED: "blocked", at.PULLING: "pulling", at.OLD: "transcript",
        at.FAILED: "failed"}  # fmt: skip
    assert rows[at.PULLING].mode is None and rows[at.PULLING].started is None
    assert rows[at.MEMO].project is None and not rows[at.BLOCKED].has_summary


def _folder(root: Root, sid: str):
    ref = _ref(sid, root)
    return root.path.joinpath(*ref.parts), ref


@pytest.mark.parametrize(
    "content",
    [
        "{not json",
        json.dumps({**at.vector("result_meeting_summary"), "v": 2}),
        json.dumps({**at.vector("result_meeting_summary"), "session": at.MEMO}),
        json.dumps({**at.vector("result_meeting_summary"), "title": "x" * 81}),
    ],
)
def test_a_bad_result_json_is_ignored(root, content):
    folder, ref = _folder(root, at.MEETING)
    (folder / "result.json").write_text(content)
    row = recs.row(root, ref)
    assert (row.state, row.title, row.todos_n) == ("processing", None, 0)  # status.json says transcribing


def test_bad_or_oversized_status_and_manifest_are_ignored(root):
    folder, ref = _folder(root, at.BLOCKED)
    (folder / "status.json").write_text(json.dumps({**at.vector("status_blocked"), "pad": "x" * 5000}))
    assert recs.row(root, ref).state == "archived"
    (folder / "manifest.json").write_text(json.dumps({**at.manifest(at.BLOCKED), "v": 2}))
    assert (recs.row(root, ref).state, recs.row(root, ref).mode) == ("pulling", None)


def test_symlinked_contract_files_are_not_followed(root, tmp_path):
    folder, ref = _folder(root, at.MEETING)
    outside = tmp_path / "outside.md"
    outside.write_text("secret")
    (folder / "summary.md").unlink()
    (folder / "summary.md").symlink_to(outside)
    assert not recs.row(root, ref).has_summary
    assert recs.detail(root, ref).summary is None


def test_detail_of_a_meeting(root):
    detail = recs.detail(root, _ref(at.MEETING, root))
    assert (detail.id, detail.state) == (at.MEETING, "done")
    assert detail.meta.segments == 3 and detail.meta.consent == "ack" and detail.meta.project == "cpa"
    assert detail.meta.marks == (Mark(12.8, "star"), Mark(361.0, "star"))
    assert detail.result.level == "summary" and detail.result.memo is None
    assert detail.result.actions == (Action(1, "给 Alice 发报价单", "我", "周五", 361.2, True),)
    assert (detail.summary, detail.truncated) == (at.MEETING_SUMMARY, False)


def test_detail_of_a_memo_and_a_pulling_session(root):
    memo = recs.detail(root, _ref(at.MEMO, root))
    assert (memo.result.memo.text, memo.result.memo.intent) == ("待办：周五前给 Alice 发报价单。", "todo")
    pulling = recs.detail(root, _ref(at.PULLING, root))
    assert (pulling.meta, pulling.result, pulling.summary) == (None, None, None)


def test_a_summary_that_cannot_be_read_says_why(root):
    folder, ref = _folder(root, at.MEETING)
    assert recs.detail(root, ref).summary_error is None
    (folder / "summary.md").write_bytes(b"x" * (recs.SUMMARY_MAX + 1))
    detail = recs.detail(root, ref)
    assert (detail.summary, detail.summary_error) == (None, "E_TOO_LARGE") and recs.row(root, ref).has_summary


def test_detail_cuts_the_summary_to_the_budget(root):
    detail = recs.detail(root, _ref(at.MEETING, root), budget=40)
    assert len(detail.summary.encode()) <= 40 and detail.truncated


def test_marks_keep_well_formed_kinds_only(root):
    folder, ref = _folder(root, at.MEETING)
    marks = [{"t_ms": 5, "k": "todo"}, {"t_ms": 6, "k": "!"}]
    (folder / "manifest.json").write_text(json.dumps({**at.manifest(at.MEETING), "marks": marks}))
    assert recs.detail(root, ref).meta.marks == (Mark(0.0, "todo"),)


def test_transcript_pages(root):
    ref = _ref(at.MEETING, root)
    page = recs.transcript(root, ref)
    assert page.sentences == (
        Sentence(0, 12.3, "我们先过一下 pricing 的方案。"), Sentence(1, 12.3, "OK."),
        Sentence(2, 361.2, "交付时间定在周五，我负责给 Alice 发报价单。"))  # fmt: skip
    assert (page.total, page.next_offset, page.truncated) == (3, None, False)
    small = recs.transcript(root, ref, offset=1, budget=60)
    assert (small.sentences, small.next_offset, small.truncated) == ((Sentence(1, 12.3, "OK."),), 2, True)


def test_transcript_falls_back_to_markdown(root):
    folder, ref = _folder(root, at.MEETING)
    (folder / "transcript.json").write_text("[]")
    page = recs.transcript(root, ref)
    assert page.sentences[0] == Sentence(0, 12.0, "我们先过一下 pricing 的方案。")
    assert page.sentences[2].t_s == 361.0 and page.total == 3


def test_transcript_cuts_one_sentence_longer_than_the_budget(root):
    folder, ref = _folder(root, at.MEETING)
    doc = {**at.vector("transcript_meeting"), "sentences": [{"t_ms": 1, "text": "长" * 100}, {"t_ms": 2, "text": "x"}]}
    (folder / "transcript.json").write_text(json.dumps(doc, ensure_ascii=False))
    page = recs.transcript(root, ref, budget=60)
    assert len(page.sentences) == 1 and page.next_offset == 1 and page.truncated


def test_transcript_missing(root):
    with pytest.raises(ArchiveError) as exc:
        recs.transcript(root, _ref(at.BLOCKED, root))
    assert exc.value.code == "E_NOT_FOUND"


def test_a_hostile_transcript_md_is_parsed_in_linear_time(root):
    folder, ref = _folder(root, at.MEETING)
    (folder / "transcript.json").unlink()
    (folder / "transcript.md").write_text("- [00:12] x" + " " * 50_000 + "z\n- [00:13] y ^t000013\n")
    started = time.monotonic()
    page = recs.transcript(root, ref, budget=1 << 20)
    assert time.monotonic() - started < 1.0
    assert page.sentences[0].text == "x" + " " * 50_000 + "z" and page.sentences[1] == Sentence(1, 13.0, "y")
