"""rec_entry (docs/11 §6.7): one daily-note line per rec.result / rec.stale, a pure function of the event."""

from __future__ import annotations

from datetime import timedelta, timezone

import pytest

from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import manifest
from cpad.recordings import records as r
from cpad.recordings.route import rec_entry
from cpad.recordings.wall import Wall

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

TZ = timezone(timedelta(hours=8))
SID = "K3M9Q2T-00042-0A1C"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
AT = "2026-10-21T06:49:02.000+00:00"
STARTED = 1792562520000  # 2026-10-21 14:02 +08:00


def session(**over) -> dict:
    return {"dir": DIR, "mode": "meeting", "project": "cpa", "started_ms": STARTED, "uncertain": False,
            "dur_ms": 2520000, **over}  # fmt: skip


def result(state="done", level="summary", title="客户同步：定价和交付", memo_text=None, memo_intent=None, actions_n=3,
           error_code=None, **over) -> dict:  # fmt: skip
    return r.result(rec=SID, state=state, level=level, title=title, memo_text=memo_text, memo_intent=memo_intent,
                    actions_n=actions_n, error_code=error_code, session=session(**over), at=AT)  # fmt: skip


def line(record) -> str:
    entry = rec_entry(record, TZ)
    assert entry is not None and entry[0] == "2026-10-21.md"
    return entry[1]


def test_meeting_done_links_the_summary_with_minutes_and_todo_count():
    assert line(result()) == (f"- 14:02 #cpa 会议：[客户同步：定价和交付]({DIR}/summary.md) 42 分钟 · 待办 3 "
                              f"^cpa-{SID.lower()}")  # fmt: skip


def test_memo_note_and_memo_todo():
    note = result(mode="memo", title="想法", memo_text="把 dedupe 放到 sid 上", memo_intent="note", actions_n=0)
    assert line(note) == f"- 14:02 #cpa 语音：把 dedupe 放到 sid 上 [音频]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"
    todo = result(mode="memo", title="报价", memo_text="周五前给 Alice 发报价单", memo_intent="todo", project=None)
    assert line(todo) == f"- [ ] 14:02 周五前给 Alice 发报价单 [音频]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"


def test_long_memo_keeps_the_first_sentence_and_links_the_full_text():
    text = "第一句话说完了。" + "后面很长" * 100
    out = line(result(mode="memo", memo_text=text, memo_intent="note"))
    assert out.startswith("- 14:02 #cpa 语音：第一句话说完了。 [全文](") and f"({DIR}/summary.md)" in out
    no_stop = line(result(mode="memo", memo_text="长" * 400, memo_intent="note"))
    assert "长" * 279 + "…" in no_stop and "长" * 281 not in no_stop


def test_transcript_only():
    assert line(result(level="transcript", title=None)) == (f"- 14:02 #cpa 会议（仅转写）：[转写]({DIR}/transcript.md) "
                                                            f"42 分钟 ^cpa-{SID.lower()}")  # fmt: skip


def test_failed_and_stale_use_the_u_block_id():
    failed = line(result(state="failed", level=None, error_code="E_STT"))
    assert failed == f"- 14:02 #cpa 会议（未转写）[音频]({DIR}/) ^cpa-{SID.lower()}-u"
    assert line(r.stale(rec=SID, session=session(), at=AT)) == failed


def test_uncertain_time_gets_a_tilde():
    assert line(result(uncertain=True)).startswith("- ~14:02 #cpa 会议：")
    todo = result(mode="memo", memo_text="买牛奶", memo_intent="todo", uncertain=True)
    assert line(todo).startswith("- [ ] ~14:02 #cpa 买牛奶")


def test_control_characters_and_brackets_are_neutralised():
    out = line(result(title="a]b[c\nd\te"))
    assert "[a\\]b\\[c d e](" in out and "\n" not in out
    memo = line(result(mode="memo", memo_text="x\ny\u0000z", memo_intent="note"))
    assert "语音：x y z [音频]" in memo


def test_title_is_capped_at_80_and_short_meetings_round_to_one_minute():
    out = line(result(title="题" * 90, dur_ms=70000))
    assert "[" + "题" * 80 + "]" in out and " 1 分钟 " in out


def test_other_events_produce_no_line():
    assert rec_entry({"ev": "rec.archived"}, TZ) is None
    assert rec_entry({"ev": "item.received", "kind": "note"}, TZ) is None
    assert rec_entry(result(state="weird"), TZ) is None


# --- v0.4.1: the memo destination (docs/13 §9.3, §11.1) ----------------------------------


def test_a_memo_sent_to_todo_gets_a_box_whatever_whisperwoofs_intent_says():
    note = result(mode="memo", memo_text="把 dedupe 放到 sid 上", memo_intent="note", dest="todo")
    assert line(note) == f"- [ ] 14:02 #cpa 把 dedupe 放到 sid 上 [音频]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"
    no_intent = result(mode="memo", memo_text="买牛奶", memo_intent=None, dest="todo", project=None)
    assert line(no_intent) == f"- [ ] 14:02 买牛奶 [音频]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"


def test_a_transcript_only_todo_memo_is_still_a_box():
    out = line(result(mode="memo", level="transcript", title=None, dest="todo"))
    assert out == f"- [ ] 14:02 #cpa 语音（仅转写）：[转写]({DIR}/transcript.md) 42 分钟 ^cpa-{SID.lower()}"


def test_dest_changes_nothing_for_unfinished_memos_meetings_daily_or_unknown_dests():
    failed = result(mode="memo", state="failed", level=None, error_code="E_STT", dest="todo")
    assert line(failed) == f"- 14:02 #cpa 语音（未转写）[音频]({DIR}/) ^cpa-{SID.lower()}-u"
    assert line(result(dest="todo")) == line(result())
    for dest in ("daily", "ww.note", None):
        memo = result(mode="memo", memo_text="想法", memo_intent="note", dest=dest)
        assert line(memo) == f"- 14:02 #cpa 语音：想法 [音频]({DIR}/seg-000.wav) ^cpa-{SID.lower()}"


def stored(dest: str | None, seg: int = 0) -> dict:
    item = FileItem(id=SID, kind="rec", size=3244, rec=SID, seg=seg, last=True, mode="memo", dur_ms=100, at=At(1000),
                    dest=dest)  # fmt: skip
    return r.seg_stored(dev="020000000001", item=item, file=f"{DIR}/seg-{seg:03d}.wav", file_sha256="0" * 64,
                        wall=Wall(STARTED, False), repaired=False, fw="cpa-fw/0.4.1", at=AT)  # fmt: skip


def test_dest_travels_from_segment_0_to_the_result_event_and_stays_absent_otherwise():
    assert stored("todo")["dest"] == "todo" and "dest" not in stored(None)
    assert manifest.dest_of((stored("todo"),)) == "todo"
    assert manifest.dest_of((stored(None),)) is None and manifest.dest_of((stored("todo", seg=1),)) is None
    doc = manifest.build((stored("todo"),), names={}, slots={}, tz_min=480, written_at=AT)
    assert "dest" not in doc  # manifest.json and recording contract v1 are unchanged
    assert manifest.archived_record(doc, dev="020000000001", dir=DIR, at=AT, dest="todo")["dest"] == "todo"
    assert "dest" not in manifest.archived_record(doc, dev="020000000001", dir=DIR, at=AT)
    archived = r.archived(dev="020000000001", rec=SID, dir=DIR, mode="memo", project=None, segments=1, dur_ms=100,
                          started_ms=STARTED, uncertain=False, gaps=[], consent=None, at=AT, dest="todo")  # fmt: skip
    assert archived["dest"] == "todo" and r.context(archived)["dest"] == "todo"
    plain = r.archived(dev="020000000001", rec=SID, dir=DIR, mode="memo", project=None, segments=1, dur_ms=100,
                       started_ms=STARTED, uncertain=False, gaps=[], consent=None, at=AT)  # fmt: skip
    assert "dest" not in plain and "dest" not in r.context(plain)
    assert "dest" not in r.stale(rec=SID, session=plain, at=AT)


# --- docs/23 §2.4: filed after the line was written ---------------------------------------


def filed(project="blog", title: str | None = "客户同步", n=1, **over) -> dict:
    return r.filed(rec=SID, project=project, session=session(**over), title=title, n=n, at=AT)


def test_a_filed_record_renders_its_own_line_with_a_block_id_per_filing():
    entry = rec_entry(filed(), TZ)
    assert entry == ("2026-10-21.md", f"- 14:02 #blog ↪ 归档：客户同步 ^cpa-{SID.lower()}-f1-blog",
                     f"^cpa-{SID.lower()}-f1-blog")  # fmt: skip
    assert line(filed(mode="memo", title=None, uncertain=True)) == (
        f"- ~14:02 #blog ↪ 归档：语音 ^cpa-{SID.lower()}-f1-blog")  # fmt: skip
    assert line(filed(title=None)).endswith("↪ 归档：会议 ^cpa-" + SID.lower() + "-f1-blog")
    assert line(filed(n=3)).endswith(f" ^cpa-{SID.lower()}-f3-blog"), "filed back to blog later: a line of its own"


def test_a_filed_title_is_one_clean_line_of_at_most_80_characters():
    out = line(filed(title="a\nb^cpa-x" + "题" * 90))
    assert "\n" not in out and "a b^cpa-x" in out and "题" * 71 in out and "题" * 72 not in out
    assert out.endswith(f" ^cpa-{SID.lower()}-f1-blog")


def test_a_filed_record_without_a_valid_project_mode_or_number_has_no_line():
    bad = (filed(project=None), filed(project="Bad Key"), filed(project="../x"), filed(mode="other"), filed(n=0),
           filed(n=None), filed(n=True), filed(n="1"))  # fmt: skip
    for record in bad:
        assert rec_entry(record, TZ) is None
