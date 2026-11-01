"""A temporary ~/CardputerNotes in archive layout v1 (docs/15 §2.1), built from the recording contract v1 vectors.

2026-10-21  1402 MEETING  manifest, stale status, transcript.json/.md, summary.md (two tasks), result done/summary
            1405 MEMO     manifest, transcript.json, summary.md, result done/summary (memo, intent todo)
            1630 BLOCKED  manifest, status blocked, no result
            1700 PULLING  one segment, no manifest yet
2026-10-20  0915 OLD      manifest, transcript.json/.md, result done/transcript
            1100 FAILED   manifest, result failed
Daily notes for both days, with typed notes, an open and a done checkbox and recording lines.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tests.support.protocol_files import PROTOCOL

VALID = PROTOCOL / "recording" / "v1" / "vectors" / "valid"
MEETING = "3QZK1M7-00042-0A1C"
MEMO = "3QZK1M7-00042-0A1F"
BLOCKED = "3QZK1M7-00043-0000"
PULLING = "3QZK1M7-00044-0000"
OLD = "3QZK1M7-00040-0000"
FAILED = "3QZK1M7-00041-0000"
MEETING_WALL = 1792620120000  # the vector's wall_ms: 2026-10-21T15:02:00-07:00 with its tz_min


def vector(name: str) -> dict[str, Any]:
    return json.loads((VALID / f"{name}.json").read_text(encoding="utf-8"))["data"]


def rec_dir(day: str, hhmm: str, sid: str) -> str:
    return f"recordings/{day}/{hhmm}-{sid.lower()}"


def manifest(sid: str, base: str = "manifest_meeting", wall_ms: int = MEETING_WALL) -> dict[str, Any]:
    doc = vector(base)
    return {**doc, "session": sid, "started": {**doc["started"], "wall_ms": wall_ms}}


def with_session(name: str, sid: str) -> dict[str, Any]:
    return {**vector(name), "session": sid}


def write(path: Path, data: str | bytes | dict) -> Path:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if isinstance(data, dict):
        data = json.dumps(data, ensure_ascii=False)
    path.write_bytes(data.encode("utf-8") if isinstance(data, str) else data)
    path.chmod(0o600)
    return path


MEETING_SUMMARY = """---
title: 客户同步：定价和交付
session: 3QZK1M7-00042-0A1C
mode: meeting
---
## 摘要
讨论了 pricing 和交付时间。

## 待办
- [ ] 给 Alice 发报价单（我，周五）[@06:01](transcript.md#^t000601) ^act-1
- [x] 确认交付时间 ^act-2
"""
MEETING_TRANSCRIPT_MD = """- [00:12] 我们先过一下 pricing 的方案。 ^t000012
- [00:12] OK. ^t000012-2
- [06:01] 交付时间定在周五，我负责给 Alice 发报价单。 ^t000601
"""
MEMO_SUMMARY = "待办：周五前给 Alice 发报价单。\n\n类型：待办\n"
OLD_TRANSCRIPT_MD = "- [00:05] 旧会议只有转写。 ^t000005\n"
NOTE_20 = (
    "- 09:20 #cpa 买牛奶 ^cpa-3qzk1m7-0000c-0005\n"
    "- [ ] 10:00 #cpa 给 Bob 回电话 ^cpa-3qzk1m7-0000c-0006\n"
    "- [x] 10:30 修好打印机 ^cpa-3qzk1m7-0000c-0007\n"
    "- 09:15 #cpa 会议（仅转写）：[转写](recordings/2026-10-20/0915-3qzk1m7-00040-0000/transcript.md) 5 分钟"
    " ^cpa-3qzk1m7-00040-0000\n"
)
NOTE_21 = (
    "- 14:02 #cpa 会议：[客户同步：定价和交付](recordings/2026-10-21/1402-3qzk1m7-00042-0a1c/summary.md) 11 分钟"
    " · 待办 1 ^cpa-3qzk1m7-00042-0a1c\n"
    "- [ ] ~14:05 待办：周五前给 Alice 发报价单。 [音频](recordings/2026-10-21/1405-3qzk1m7-00042-0a1f/seg-000.wav)"
    " ^cpa-3qzk1m7-00042-0a1f\n"
    "- 15:00 #home Pricing idea for the garden ^cpa-3qzk1m7-0000d-0001\n"
)
WAV = b"RIFF" + bytes(40) + b"\x01\x00" * 8


def _meeting(notes: Path) -> None:
    folder = notes / rec_dir("2026-10-21", "1402", MEETING)
    write(folder / "manifest.json", manifest(MEETING))
    write(folder / "status.json", vector("status_transcribing"))
    write(folder / "transcript.json", vector("transcript_meeting"))
    write(folder / "transcript.md", MEETING_TRANSCRIPT_MD)
    write(folder / "summary.md", MEETING_SUMMARY)
    write(folder / "result.json", vector("result_meeting_summary"))
    write(folder / "seg-000.wav", WAV)
    write(folder / ".result.json.tmp", "partial")


def _memo(notes: Path) -> None:
    folder = notes / rec_dir("2026-10-21", "1405", MEMO)
    write(folder / "manifest.json", manifest(MEMO, "manifest_memo", MEETING_WALL + 180_000))
    write(folder / "transcript.json", {**with_session("transcript_meeting", MEMO), "sentences": [
        {"t_ms": 0, "text": "待办：周五前给 Alice 发报价单。"}]})  # fmt: skip
    write(folder / "summary.md", MEMO_SUMMARY)
    write(folder / "result.json", vector("result_memo_todo"))
    write(folder / "seg-000.wav", WAV)


def _others(notes: Path) -> None:
    blocked = notes / rec_dir("2026-10-21", "1630", BLOCKED)
    write(blocked / "manifest.json", manifest(BLOCKED, wall_ms=MEETING_WALL + 8_880_000))
    write(blocked / "status.json", with_session("status_blocked", BLOCKED))
    write(notes / rec_dir("2026-10-21", "1700", PULLING) / "seg-000.wav", WAV)
    old = notes / rec_dir("2026-10-20", "0915", OLD)
    write(old / "manifest.json", manifest(OLD, wall_ms=MEETING_WALL - 103_620_000))
    write(old / "transcript.json", {**with_session("transcript_meeting", OLD), "sentences": [
        {"t_ms": 5000, "text": "旧会议只有转写。"}]})  # fmt: skip
    write(old / "transcript.md", OLD_TRANSCRIPT_MD)
    write(old / "result.json", with_session("result_transcript_only", OLD))
    failed = notes / rec_dir("2026-10-20", "1100", FAILED)
    write(failed / "manifest.json", manifest(FAILED, wall_ms=MEETING_WALL - 97_320_000))
    write(failed / "result.json", with_session("result_failed", FAILED))


def build(notes: Path) -> Path:
    """Fill `notes` (created 0700) with the archive above and return it."""
    notes.mkdir(mode=0o700, parents=True, exist_ok=True)
    write(notes / "2026-10-20.md", NOTE_20)
    write(notes / "2026-10-21.md", NOTE_21)
    write(notes / "ideas.md", "not a daily note\n")
    write(notes / ".obsidian" / "app.json", "{}")
    _meeting(notes)
    _memo(notes)
    _others(notes)
    return notes
