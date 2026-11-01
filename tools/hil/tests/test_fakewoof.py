"""fakewoof: the WhisperWoof side of recording contract v1 (docs/11 §7.2-§7.7, protocol/recording/v1),
deterministic, for the HIL. Every valid manifest vector round-trips to outputs both the reference
checker and cpad's own reader accept; invalid manifests get a failed result with the vector's code."""

import asyncio
import copy
import hashlib
import json
import os
import stat
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest
from cpad.recordings import contract as cpad_contract

from harness.fakewoof import (
    MONOID,
    FakeWoof,
    check_doc,
    intent_of,
    md_block_ids,
    vectors,
)
from harness.rec_checks import wav_header

SID = "K3M9Q2T-00042-0A1C"
NOW = datetime(2026, 10, 21, 14, 49, 2, tzinfo=timezone(timedelta(hours=8)))
VALID = [c for c in vectors("valid") if c["doc"] == "manifest"]
INVALID = [c for c in vectors("invalid") if c["doc"] == "manifest"]
OUTPUT_FILES = ("transcript.json", "transcript.md", "summary.md")


def place(root: Path, manifest, sid: str = SID) -> Path:
    folder = root / "2026-10-21" / f"1402-{sid.lower()}"
    folder.mkdir(parents=True)
    (folder / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False))
    return folder


def woof(root: Path, text: str = "HIL 测试", verify_audio: bool = False) -> FakeWoof:
    return FakeWoof(root, text_for=lambda _m: text, verify_audio=verify_audio, now=lambda: NOW)


def read(folder: Path, name: str):
    return json.loads((folder / name).read_text())


def test_the_contract_vectors_are_there():
    assert len(VALID) >= 3 and len(INVALID) >= 8


@pytest.mark.parametrize("case", VALID, ids=lambda c: c["name"][:40])
def test_every_valid_manifest_vector_round_trips_to_valid_outputs(tmp_path, case):
    manifest = case["data"]
    folder = place(tmp_path, manifest, manifest["session"])
    (outcome,) = woof(tmp_path).scan()
    assert (outcome.session, outcome.state, outcome.level, outcome.error_code) == (manifest["session"], "done",
                                                                                   "summary", None)  # fmt: skip
    result, status = read(folder, "result.json"), read(folder, "status.json")
    transcript = read(folder, "transcript.json")
    assert check_doc("result", result) == ("ok", ())
    assert check_doc("status", status) == ("ok", ())
    assert check_doc("transcript", transcript) == ("ok", ())
    assert cpad_contract.check_result(result).ok and cpad_contract.check_status(status).ok
    assert (result["session"], result["mode"], result["attempt"]) == (manifest["session"], manifest["mode"], 1)
    for key, out in result["outputs"].items():
        assert hashlib.sha256((folder / out["path"]).read_bytes()).hexdigest() == out["sha256"], key
    assert (result["memo"] is None) == (manifest["mode"] == "meeting")


@pytest.mark.parametrize("case", INVALID, ids=lambda c: c["name"][:40])
def test_invalid_manifests_get_a_failed_result_with_the_vector_code(tmp_path, case):
    named = case["data"].get("session")
    sid = named if isinstance(named, str) and MONOID.match(named) else SID  # else told by the directory name
    folder = place(tmp_path, case["data"], sid)
    (outcome,) = woof(tmp_path).scan()
    result = read(folder, "result.json")
    assert (outcome.state, outcome.error_code) == ("failed", case["reason"]["code"])
    assert result["state"] == "failed" and result["error"]["code"] == case["reason"]["code"]
    assert result["session"] == sid
    assert check_doc("result", result) == ("ok", ()) and cpad_contract.check_result(result).ok
    assert not any((folder / name).exists() for name in OUTPUT_FILES)


def test_unknown_manifest_fields_are_ignored(tmp_path):
    manifest = copy.deepcopy(VALID[0]["data"])
    manifest = {**manifest, "x_future": {"a": 1}, "segments": [{**s, "x_seg": True} for s in manifest["segments"]]}
    place(tmp_path, manifest, manifest["session"])
    (outcome,) = woof(tmp_path).scan()
    assert outcome.state == "done"


def _memo():
    return next(c["data"] for c in VALID if c["data"]["mode"] == "memo")


@pytest.mark.parametrize(
    ("text", "intent"), [("待办：周五发报价", "todo"), ("今天的想法", "note"), ("我的待办", "note")]
)
def test_memo_intent_is_todo_only_when_the_fake_text_starts_with_daiban(tmp_path, text, intent):
    assert intent_of(text) == intent
    memo = _memo()
    folder = place(tmp_path, memo, memo["session"])
    woof(tmp_path, text=text).scan()
    assert read(folder, "result.json")["memo"] == {"text": text, "intent": intent}


def test_outputs_are_written_in_contract_order_result_last_with_no_temp_files_left(tmp_path):
    memo = _memo()
    folder = place(tmp_path, memo, memo["session"])
    (outcome,) = woof(tmp_path).scan()
    assert outcome.written[0] == "status.json" and outcome.written[-1] == "result.json"
    assert outcome.written.index("transcript.json") < outcome.written.index("summary.md")
    assert not [p.name for p in folder.iterdir() if p.name.startswith(".")]
    assert all(stat.S_IMODE(p.stat().st_mode) == 0o600 for p in folder.iterdir() if p.name != "manifest.json")


def test_a_session_with_a_result_is_left_alone(tmp_path):
    memo = _memo()
    folder = place(tmp_path, memo, memo["session"])
    fw = woof(tmp_path)
    assert len(fw.scan()) == 1
    before = {p.name: p.stat().st_mtime_ns for p in folder.iterdir()}
    assert fw.scan() == ()
    assert woof(tmp_path).scan() == ()  # a fresh instance goes by result.json, not memory
    assert {p.name: p.stat().st_mtime_ns for p in folder.iterdir()} == before


def test_folders_without_a_manifest_and_symlinked_sessions_are_ignored(tmp_path):
    (tmp_path / "2026-10-21" / "1402-incomplete").mkdir(parents=True)
    memo = _memo()
    real = place(tmp_path / "elsewhere", memo, memo["session"])
    os.symlink(real, tmp_path / "2026-10-21" / f"1403-{memo['session'].lower()}")
    assert woof(tmp_path).scan() == ()
    assert not (real / "result.json").exists()


def _session_with_audio(root: Path, pcm: bytes) -> tuple[Path, dict]:
    blob = wav_header(len(pcm)) + pcm
    seg = {"i": 0, "file": "seg-000.wav", "id": SID, "t0_ms": 0, "samples": len(pcm) // 2, "bytes": len(blob),
           "sha256": hashlib.sha256(blob).hexdigest(), "recovered": False, "repaired": False}  # fmt: skip
    manifest = {**_memo(), "session": SID, "segments": [seg], "duration_ms": len(pcm) // 32}
    folder = place(root, manifest)
    (folder / "seg-000.wav").write_bytes(blob)
    return folder, manifest


def test_audio_is_verified_when_asked(tmp_path):
    folder, _ = _session_with_audio(tmp_path / "good", bytes(range(256)) * 4)
    (outcome,) = woof(tmp_path / "good", verify_audio=True).scan()
    assert outcome.state == "done"
    folder, _ = _session_with_audio(tmp_path / "bad", bytes(range(256)) * 4)
    blob = bytearray((folder / "seg-000.wav").read_bytes())
    blob[100] ^= 0xFF
    (folder / "seg-000.wav").write_bytes(bytes(blob))
    (outcome,) = woof(tmp_path / "bad", verify_audio=True).scan()
    assert (outcome.state, outcome.error_code) == ("failed", "E_INVALID")


def test_transcript_md_block_ids_are_unique_within_one_second():
    assert md_block_ids([12_340, 12_900, 754_000, 3_600_000]) == ("t000012", "t000012-2", "t001234", "t010000")


def test_meeting_summary_lists_every_star_under_the_highlights(tmp_path):
    meeting = next(c["data"] for c in VALID if c["data"]["mode"] == "meeting" and c["data"]["marks"])
    folder = place(tmp_path, meeting, meeting["session"])
    woof(tmp_path).scan()
    summary = (folder / "summary.md").read_text()
    assert summary.startswith("---\n") and "source: cardputer" in summary and "## 重点（你的标记）" in summary
    highlights = summary.split("## 重点（你的标记）")[1].split("\n## ")[0]
    assert highlights.count("](transcript.md#^t") == len(meeting["marks"])


def test_watch_processes_sessions_as_they_appear(tmp_path):
    fw, memo = woof(tmp_path), _memo()

    async def run() -> None:
        stop = asyncio.Event()
        task = asyncio.create_task(fw.watch(stop, interval=0.01))
        folder = place(tmp_path, memo, memo["session"])
        for _ in range(200):
            if (folder / "result.json").exists():
                break
            await asyncio.sleep(0.01)
        stop.set()
        await task

    asyncio.run(run())
    assert [o.session for o in fw.outcomes] == [memo["session"]]


def test_a_session_whose_id_cannot_be_told_is_left_alone(tmp_path):
    folder = tmp_path / "2026-10-21" / "1402-not-a-session"
    folder.mkdir(parents=True)
    (folder / "manifest.json").write_text(json.dumps({**_memo(), "session": "nope"}))
    assert woof(tmp_path).scan() == ()
    assert not (folder / "result.json").exists()
