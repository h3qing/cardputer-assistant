"""Literal, case-insensitive search over notes, summaries and transcripts (docs/15 §3.4): at most PER_SOURCE hits
per source, resumable positions, and caps that mark a result partial."""

from __future__ import annotations

from datetime import date
from itertools import count

import pytest

from cpad.archive import notes, recordings
from cpad.archive import search as s
from cpad.archive.fs import Root
from cpad.archive.search import Hit
from tests.support import archive_tree as at


@pytest.fixture
def root(tmp_path):
    return Root.at(at.build(tmp_path / "notes"))


def test_find_hits_is_literal_and_case_insensitive():
    text = "first line\nPricing (.*) here\nno\nprice and PRICING"
    assert s.find_hits(text, "pricing") == ((2, "Pricing (.*) here"), (4, "price and PRICING"))
    assert s.find_hits(text, "(.*)") == ((2, "Pricing (.*) here"),)
    assert s.find_hits(text, "absent") == ()


def test_snippets_are_cut_around_the_match():
    line = "a" * 300 + "needle" + "b" * 300
    snip = s.snippet(line, "NEEDLE", 60)
    assert snip == "…" + "a" * 60 + "needle" + "b" * 60 + "…"
    assert s.snippet("ß needle", "NEEDLE") == "ß needle"  # casefold changes lengths: still a snippet


MEETING_DIR = "2026-10-21/0/1402-3qzk1m7-00042-0a1c"
BUDGET = "3QZK1M7-00045-0000"


def test_search_everything_newest_first(root):
    found = s.search(root, "PRICING")
    assert found.hits == (
        Hit("notes", "2026-10-21", 3, "- 15:00 #home Pricing idea for the garden ^cpa-3qzk1m7-0000d-0001", 2, 1,
            ("2026-10-21/1", 0)),
        Hit("summaries", at.MEETING, 7, "讨论了 pricing 和交付时间。", 6, 1, (f"{MEETING_DIR}/1", 0)),
        Hit("transcripts", at.MEETING, 12.3, "我们先过一下 pricing 的方案。", 0, 1, (f"{MEETING_DIR}/0", 0)),
    )  # fmt: skip
    assert not found.partial and found.scanned > 0 and found.next is None


def _long_meeting(root):
    """A 16:40 meeting whose transcript says 预算 60 times; the 14:02 meeting's summary says it once."""
    folder = root.path / at.rec_dir("2026-10-21", "1640", BUDGET)
    at.write(folder / "manifest.json", at.manifest(BUDGET))
    said = [{"t_ms": k * 10_000, "text": f"第 {k} 次说到预算。"} for k in range(60)]
    at.write(folder / "transcript.json", {**at.with_session("transcript_meeting", BUDGET), "sentences": said})
    summary = root.path / at.rec_dir("2026-10-21", "1402", at.MEETING) / "summary.md"
    summary.write_text(summary.read_text(encoding="utf-8") + "预算还没定。\n", encoding="utf-8")


def test_one_long_transcript_cannot_crowd_out_other_meetings(root):
    _long_meeting(root)
    found = s.search(root, "预算")
    assert [(h.kind, h.ref) for h in found.hits] == [("transcripts", BUDGET)] * 3 + [("summaries", at.MEETING)]
    assert [h.matches for h in found.hits] == [60, 60, 60, 1] and [h.i for h in found.hits][:3] == [0, 1, 2]
    assert not found.partial and found.next is None


def _all_pages(root, query, limit):
    pages, after = [], None
    while True:
        page = s.search(root, query, limit=limit, after=after)
        pages.append(page)
        if page.next is None:
            return pages
        after = page.next


@pytest.mark.parametrize("limit", [1, 2, 3])
def test_pages_resume_where_the_last_one_stopped(root, limit):
    _long_meeting(root)
    everything = s.search(root, "报价单", limit=50).hits + s.search(root, "预算", limit=50).hits
    paged = [h for q in ("报价单", "预算") for page in _all_pages(root, q, limit) for h in page.hits]
    assert paged == list(everything) and len(paged) == 9


def test_the_limit_is_not_partial_and_next_means_a_further_hit_exists(root):
    every = s.search(root, "报价单").hits
    assert len(every) == 5
    exact = s.search(root, "报价单", limit=5)
    assert len(exact.hits) == 5 and exact.next is None and not exact.partial
    short = s.search(root, "报价单", limit=4)
    assert short.next == every[4].pos and not short.partial


def test_caps_mark_the_result_partial_and_the_scan_resumes(root, monkeypatch):
    everything = s.search(root, "pricing").hits
    monkeypatch.setattr(s, "FILES_MAX", 1)
    capped = s.search(root, "pricing")
    assert (capped.scanned, capped.partial, len(capped.hits)) == (1, True, 1) and capped.next is not None
    pages = _all_pages(root, "pricing", 20)
    assert [h for page in pages for h in page.hits] == list(everything) and len(everything) == 3
    assert all(page.partial for page in pages[:-1]) and not pages[-1].partial


def test_hit_indexes_are_the_offsets_the_readers_take(root):
    said = next(h for h in s.search(root, "周五", scopes=("transcripts",)).hits if h.ref == at.MEETING)
    page = recordings.transcript(root, recordings.find(root, at.MEETING), offset=said.i)
    assert "周五" in page.sentences[0].text and page.sentences[0].i == said.i
    written = s.search(root, "Bob", scopes=("notes",)).hits[0]
    assert written.where == written.i + 1
    assert "Bob" in notes.read_day(root, date(2026, 10, 20), offset=written.i).text.splitlines()[0]


def test_positions_round_trip():
    pos = (f"{MEETING_DIR}/1", 2)
    assert s.parse_pos(s.format_pos(pos)) == pos
    assert s.parse_pos("2026-10-21/1#0") == ("2026-10-21/1", 0)


@pytest.mark.parametrize("text", ["", "x", "2026-10-21/1#3", "2026-13-40/1#0", "2026-10-21/2#0", "2026-10-21/1#0 ",
                                  f"{MEETING_DIR}/2#0", "2026-10-21/0/1402-../../x/1#0", 5])  # fmt: skip
def test_bad_positions(text):
    with pytest.raises(ValueError):
        s.parse_pos(text)


def test_search_scopes_range_and_hidden(root):
    assert [h.kind for h in s.search(root, "pricing", scopes=("transcripts",)).hits] == ["transcripts"]
    assert s.search(root, "报价单", end=date(2026, 10, 20)).hits == ()
    assert len(s.search(root, "报价单").hits) == 5
    hidden = frozenset({at.MEETING.lower()})
    assert [h.kind for h in s.search(root, "pricing", hidden=hidden).hits] == ["notes"]


def test_the_time_cap(root):
    ticks = count(0, 10)
    found = s.search(root, "pricing", clock=lambda: float(next(ticks)))
    assert found.partial and found.scanned <= 1


@pytest.mark.parametrize(("query", "scopes"), [("", ("notes",)), ("x", ()), ("x", ("notes", "audio"))])
def test_bad_arguments(root, query, scopes):
    with pytest.raises(ValueError):
        s.search(root, query, scopes=scopes)
