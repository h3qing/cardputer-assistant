"""cpa-stt end to end on temp recording folders with a fake transcriber command (docs/22 §4.6-§4.9):
status.json -> transcript.json -> transcript.md -> result.json (last), never touching a folder that has
result.json, the phantom guards, the contract codes, meetings and the order."""

from __future__ import annotations

import hashlib
import stat

import pytest

from cpa_stt import config, loop, manifest, process, writer
from cpad.recordings import contract
from tests.support import stt_tree
from tests.support.stt_rig import T0, failed, make_rig
from tests.support.stt_tree import fake_argv, make_session, read_json

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture


@pytest.fixture
def rig(tmp_path):
    return make_rig(tmp_path)


def _private(folder):
    return {p.name: stat.S_IMODE(p.stat().st_mode) for p in folder.iterdir() if p.suffix != ".wav"}


def test_off_by_default_writes_nothing(rig):
    folder = make_session(rig.root)
    report = rig.once()
    assert report.state == "off"
    assert sorted(p.name for p in folder.iterdir()) == ["manifest.json", "seg-000.wav"]
    assert not (rig.root / ".transcriber").exists()


def test_a_memo_end_to_end(rig, monkeypatch):
    order = []
    write_file, place_result = writer.write_file, writer.place_result
    monkeypatch.setattr(writer, "write_file", lambda f, n, d: order.append(n) or write_file(f, n, d))
    monkeypatch.setattr(writer, "place_result", lambda f, d: order.append("result.json") or place_result(f, d))
    folder = make_session(rig.root, lead_ms=200)
    rig.configure("--say", "记一下", "--say", "买牛奶")
    report = rig.once()
    assert (report.state, report.done) == ("ok", 1)
    assert order == ["status.json", "transcript.json", "transcript.md", "result.json"]
    result = read_json(folder / "result.json")
    assert contract.check_result(result).ok
    assert result["memo"] == {"text": "记一下 买牛奶", "intent": "note"} and result["pipeline"]["app"] == "cpa-stt"
    for key, name in (("transcript_json", "transcript.json"), ("transcript_md", "transcript.md")):
        assert result["outputs"][key]["sha256"] == hashlib.sha256((folder / name).read_bytes()).hexdigest()
    sentences = read_json(folder / "transcript.json")["sentences"]
    assert sentences == [{"t_ms": 200, "text": "记一下"}, {"t_ms": 2200, "text": "买牛奶"}]
    assert (folder / "transcript.md").read_text() == "- [00:00] 记一下 ^t000000\n- [00:02] 买牛奶 ^t000002\n"
    assert read_json(folder / "status.json")["by"] == "cpa-stt"
    assert set(_private(folder).values()) == {0o600} and not [p for p in folder.iterdir() if p.name[0] == "."]
    assert not (rig.root / ".transcriber").exists()
    assert rig.once().done == 0 and rig.runs() == [16000, 16000]  # the self-test, then the memo


def test_a_meeting_with_a_gap_is_two_runs_in_session_time(rig):
    blobs = [stt_tree.tone(1600), stt_tree.tone(3200), stt_tree.tone(1600)]
    folder = make_session(rig.root, blobs=blobs, indices=[0, 1, 3], gaps=[2], mode="meeting", lead_ms=150)
    rig.configure("--describe")
    assert rig.once().done == 1
    texts = [(s["t_ms"], s["text"]) for s in read_json(folder / "transcript.json")["sentences"]]
    assert texts == [(0, "samples=4800 offset=150"), (180000, "samples=1600 offset=0")]
    result = read_json(folder / "result.json")
    assert result["mode"] == "meeting" and result["memo"] is None and contract.check_result(result).ok


def test_meetings_can_be_left_out(rig):
    folder = make_session(rig.root, mode="meeting")
    rig.configure("--say", "x", meetings=False)
    assert rig.once().done == 0 and not (folder / "result.json").exists()


def test_a_folder_with_result_json_is_never_touched(rig):
    folder = make_session(rig.root)
    (folder / "result.json").write_text("theirs")
    before = sorted(p.name for p in folder.iterdir())
    rig.configure("--say", "x")
    rig.once()
    assert sorted(p.name for p in folder.iterdir()) == before and (folder / "result.json").read_text() == "theirs"


def test_a_result_that_appears_midway_stops_the_writes(rig, monkeypatch):
    folder = make_session(rig.root)
    write_file = writer.write_file

    def racing(f, name, data):
        ok = write_file(f, name, data)
        if name == "transcript.json":
            (f / "result.json").write_text("theirs")
        return ok

    monkeypatch.setattr(writer, "write_file", racing)
    rig.configure("--say", "x")
    report = rig.once()
    assert report.done == 0
    assert (folder / "result.json").read_text() == "theirs" and not (folder / "transcript.md").exists()


def test_silence_never_reaches_the_command_and_says_nothing_heard(rig):
    folder = make_session(rig.root, blobs=[stt_tree.silence(16000)])
    rig.configure("--say", "优优独播剧场——YoYo Television Series Exclusive")
    assert rig.once().done == 1
    assert rig.runs() == [16000]  # the self-test only
    result = read_json(folder / "result.json")
    assert result["state"] == "done" and result["memo"] is None
    assert read_json(folder / "transcript.json")["sentences"] == []
    assert (folder / "transcript.md").read_text() == "（没听到）\n"


def test_phantoms_prompt_echo_and_drop_lines_give_no_text(rig):
    folder = make_session(rig.root)
    rig.configure("--prompt", "Cardputer, WhisperWoof", "--say", "明镜需要您的支持 欢迎订阅明镜",
                  "--say", "Cardputer, WhisperWoof.", "--say", "嗯嗯", drop_lines=["嗯嗯"])  # fmt: skip
    rig.once()
    assert read_json(folder / "result.json")["memo"] is None
    assert read_json(folder / "transcript.json")["sentences"] == []


def test_bad_recordings_fail_with_the_contract_codes(rig):
    v2 = make_session(rig.root, "3QZK1M7-00042-0A40", doc={**stt_tree.manifest("3QZK1M7-00042-0A40", [
        (0, stt_tree.tone(160), 0)]), "v": 2})  # fmt: skip
    sha = make_session(rig.root, "3QZK1M7-00042-0A41")
    (sha / "seg-000.wav").write_bytes(stt_tree.tone(16000)[:-2] + b"\x00\x01")
    header = make_session(rig.root, "3QZK1M7-00042-0A42")
    (header / "seg-000.wav").write_bytes(b"RIFX" + (header / "seg-000.wav").read_bytes()[4:])
    rig.configure("--say", "x")
    report = rig.once()
    assert report.failed == 3
    assert failed(v2) == {"code": "E_VERSION", "retryable": False}
    assert failed(sha) == {"code": "E_INVALID", "retryable": False}
    assert failed(header) == {"code": "E_AUDIO", "retryable": False}


def test_what_cannot_be_trusted_gets_nothing_written(rig, tmp_path):
    unreadable = make_session(rig.root, "3QZK1M7-00042-0A43")
    (unreadable / "manifest.json").write_text("{")
    nameless = make_session(rig.root, "3QZK1M7-00042-0A44", doc={"v": 1, "session": "nope", "mode": "memo"})
    linked = make_session(rig.root, "3QZK1M7-00042-0A45")
    (linked / "seg-000.wav").rename(tmp_path / "real.wav")
    (linked / "seg-000.wav").symlink_to(tmp_path / "real.wav")
    rig.configure("--say", "x")
    rig.once()
    for folder in (unreadable, nameless, linked):
        assert sorted(p.name for p in folder.iterdir()) == ["manifest.json", "seg-000.wav"], folder


def test_clips_first_then_newest_memos_then_meetings(rig):
    make_session(rig.root, "3QZK1M7-00042-0A50", [stt_tree.tone(1000)], mode="meeting", wall_ms=1792620900000)
    make_session(rig.root, "3QZK1M7-00042-0A51", [stt_tree.tone(2000)], wall_ms=1792620000000)
    make_session(rig.root, "3QZK1M7-00042-0A52", [stt_tree.tone(3000)], wall_ms=1792620800000)
    make_session(rig.root, "3QZK1M7-00042-0A53", [stt_tree.tone(4000)], cap="3QZK1M7-00042-0A49",
                 wall_ms=1792610000000)  # fmt: skip
    rig.configure("--say", "x")
    assert rig.once().done == 4
    assert rig.runs() == [16000, 4000, 3000, 2000, 1000]


def test_watch_polls_until_stopped_and_releases_the_claim(rig):
    make_session(rig.root)
    rig.configure("--say", "x")
    reports = []
    loop.watch(rig.deps, interval=0.01, stop=lambda: len(reports) >= 2, sleep=lambda s: None, report=reports.append)
    assert [r.state for r in reports] == ["ok", "ok"] and reports[0].done == 1
    assert not (rig.root / ".transcriber").exists()


def test_no_recordings_folder_yet(rig):
    rig.root.rmdir()
    rig.configure("--say", "x")
    assert rig.once().state == "no_recordings"


def test_blocked_statuses_leave_meetings_alone_when_meetings_are_off(rig):
    memo = make_session(rig.root, "3QZK1M7-00042-0A72")
    meeting = make_session(rig.root, "3QZK1M7-00042-0A73", mode="meeting")
    rig.configure("--exit", "1", meetings=False)
    assert rig.once().state == "blocked"
    assert read_json(memo / "status.json")["state"] == "blocked" and not (meeting / "status.json").exists()


def test_with_meetings_off_even_an_invalid_meeting_gets_nothing(rig):
    sid = "3QZK1M7-00042-0A93"
    doc = {**stt_tree.manifest(sid, [(0, stt_tree.tone(160), 0)], mode="meeting"), "v": 2}
    folder = make_session(rig.root, sid, doc=doc)
    rig.configure("--say", "x", meetings=False)
    rig.once()
    assert sorted(p.name for p in folder.iterdir()) == ["manifest.json", "seg-000.wav"]


def test_blocked_statuses_skip_a_folder_with_a_symlinked_segment(rig, tmp_path):
    linked = make_session(rig.root, "3QZK1M7-00042-0A94")
    (linked / "seg-000.wav").rename(tmp_path / "real.wav")
    (linked / "seg-000.wav").symlink_to(tmp_path / "real.wav")
    rig.configure("--exit", "1")
    assert rig.once().state == "blocked"
    assert sorted(p.name for p in linked.iterdir()) == ["manifest.json", "seg-000.wav"]


def test_output_beyond_what_the_recording_can_hold_is_a_failed_run(rig, tmp_path):
    folder = make_session(rig.root, blobs=[stt_tree.tone(1600)])
    ctx = process.Ctx(config.parse_config({"provider": "command", "argv": fake_argv("--flood")}), rig.deps.env,
                      now=lambda: loop.iso(T0), tick=lambda: True)  # fmt: skip
    heard = process.hear(folder, manifest.runs(read_json(folder / "manifest.json"))[0], ctx)
    assert heard.failed is not None and heard.failed.truncated and heard.sentences == ()
