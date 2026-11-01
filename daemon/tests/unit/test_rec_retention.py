"""30-day audio retention (user decision 2026-10-20): WAVs go, text stays forever."""

from __future__ import annotations

import os

from cpad.protocol.file_messages import At, FileItem
from cpad.recordings import index, retention
from cpad.recordings import records as r
from cpad.recordings.wall import Wall

DEV = "020000000001"
SID = "K3M9Q2T-00042-0A1C"
DIR = f"recordings/2026-10-21/1402-{SID.lower()}"
ARCHIVED_AT = "2026-10-21T06:45:10.000+00:00"
ARCHIVED_MS = 1792565110000
DAY_MS = 86_400_000
SESSION = {"dir": DIR, "mode": "meeting", "project": None, "started_ms": 1, "uncertain": False, "dur_ms": 1}


def stored(i: int, file: str | None = None) -> dict:
    seg = f"K3M9Q2T-00042-0A1{'CDE'[i]}"
    item = FileItem(id=seg, kind="rec", size=48, rec=SID, seg=i, last=i == 1, mode="meeting", dur_ms=0, at=At(1))
    return r.seg_stored(dev=DEV, item=item, file=file or f"{DIR}/seg-{i:03d}.wav", file_sha256="0" * 64,
                        wall=Wall(1, False), repaired=False, fw="cpa-fw/0.3.0", at=ARCHIVED_AT)  # fmt: skip


def history(state: str = "done") -> list[dict]:
    archived = r.archived(dev=DEV, rec=SID, dir=DIR, mode="meeting", project=None, segments=2, dur_ms=1,
                          started_ms=1, uncertain=False, gaps=[], consent=None, at=ARCHIVED_AT)  # fmt: skip
    done = r.result(rec=SID, state=state, level="summary" if state == "done" else None, title=None, memo_text=None,
                    memo_intent=None, actions_n=0, error_code=None, session=SESSION, at=ARCHIVED_AT)  # fmt: skip
    return [stored(0), stored(1), archived, done]


def make_session(tmp_path, *, transcript=True, summary=True) -> None:
    folder = tmp_path / DIR
    folder.mkdir(parents=True)
    for name in ("seg-000.wav", "seg-001.wav", "manifest.json", "result.json", "transcript.md"):
        (folder / name).write_bytes(b"x")
    if transcript:
        (folder / "transcript.json").write_bytes(b"{}")
    if summary:
        (folder / "summary.md").write_bytes(b"# s")


def names(tmp_path) -> set[str]:
    return {p.name for p in (tmp_path / DIR).iterdir()}


def test_wavs_older_than_30_days_go_and_text_stays(tmp_path):
    make_session(tmp_path)
    out = retention.sweep(tmp_path, index.fold(history()), ARCHIVED_MS + 30 * DAY_MS)
    assert [(e["ev"], e["rec"], e["files"]) for e in out] == [("rec.audio.swept", SID, 2)]
    assert names(tmp_path) == {"manifest.json", "result.json", "transcript.md", "transcript.json", "summary.md"}
    swept = index.fold(out, index.fold(history()))
    assert retention.sweep(tmp_path, swept, ARCHIVED_MS + 40 * DAY_MS) == ()


def test_nothing_goes_before_30_days(tmp_path):
    make_session(tmp_path)
    assert retention.sweep(tmp_path, index.fold(history()), ARCHIVED_MS + 30 * DAY_MS - 1) == ()
    assert "seg-000.wav" in names(tmp_path)


def test_audio_stays_without_a_transcript_a_summary_or_a_done_result(tmp_path):
    for kwargs in ({"transcript": False}, {"summary": False}):
        make_session(tmp_path, **kwargs)
        assert retention.sweep(tmp_path, index.fold(history()), ARCHIVED_MS + 60 * DAY_MS) == ()
        assert "seg-001.wav" in names(tmp_path)
        for p in (tmp_path / DIR).iterdir():
            p.unlink()
        (tmp_path / DIR).rmdir()
    make_session(tmp_path)
    assert retention.sweep(tmp_path, index.fold(history("failed")), ARCHIVED_MS + 60 * DAY_MS) == ()


def test_only_regular_seg_files_inside_the_session_are_removed(tmp_path):
    make_session(tmp_path)
    outside = tmp_path / "keep.wav"
    outside.write_bytes(b"k")
    os.unlink(tmp_path / DIR / "seg-001.wav")
    os.symlink(outside, tmp_path / DIR / "seg-001.wav")
    events = [stored(0), stored(1), stored(2, file="../keep.wav"), *history()[2:]]
    out = retention.sweep(tmp_path, index.fold(events), ARCHIVED_MS + 31 * DAY_MS)
    assert out[0]["files"] == 1 and outside.exists() and (tmp_path / DIR / "seg-001.wav").is_symlink()


def test_a_missing_session_folder_is_skipped(tmp_path):
    assert retention.sweep(tmp_path, index.fold(history()), ARCHIVED_MS + 31 * DAY_MS) == ()
