"""cpa-stt when things go wrong (docs/22 §4.6): back-off, blocked environments, retry (also into a running
watch), unreadable or unwritable folders, the log and the state file."""

from __future__ import annotations

import json
import os

import pytest

from cpa_stt import config, loop, manifest, process, runner, state
from cpad.recordings import contract
from tests.support.stt_rig import T0, failed, make_rig
from tests.support.stt_tree import fake_argv, make_session, read_json


@pytest.fixture
def rig(tmp_path):
    return make_rig(tmp_path)


def _breakable(tmp_path):
    """A folder whose `broken` file makes the --fail-while command fail (the environment, not one file)."""
    flag = tmp_path / "env"
    flag.mkdir()
    (flag / "broken").touch()
    return flag


def test_retry_removes_only_our_failed_results_and_unblocks(rig):
    ours = make_session(rig.root, "3QZK1M7-00042-0A60")
    theirs = make_session(rig.root, "3QZK1M7-00042-0A61")
    done = make_session(rig.root, "3QZK1M7-00042-0A62")
    (ours / "result.json").write_text(json.dumps({"state": "failed", "pipeline": {"app": "cpa-stt"}}))
    (theirs / "result.json").write_text(json.dumps({"state": "failed", "pipeline": {"app": "WhisperWoof/2.9.0"}}))
    (done / "result.json").write_text(json.dumps({"state": "done", "pipeline": {"app": "cpa-stt"}}))
    state.save(rig.paths.state, state.SttState(blocked="engine", config_sig="x"))
    rig.configure("--say", "x")
    assert loop.retry(rig.deps) == 1
    assert not (ours / "result.json").exists() and (theirs / "result.json").exists() and (done / "result.json").exists()
    assert state.load(rig.paths.state).blocked is None


def test_retry_unblocks_a_running_watch(rig, tmp_path):
    folder = make_session(rig.root)
    flag = _breakable(tmp_path)
    rig.configure("--fail-while", str(flag), "--say", "x")
    reports = []

    def between_passes(_):
        if len(reports) == 1:  # fixed, then `cpa-stt retry` (another process) while watch keeps running
            (flag / "broken").unlink()
            loop.retry(rig.deps)

    loop.watch(rig.deps, interval=0, stop=lambda: len(reports) >= 2, sleep=between_passes, report=reports.append)
    assert [r.state for r in reports] == ["blocked", "ok"] and reports[1].done == 1
    assert (folder / "result.json").exists() and not rig.paths.retry.exists()


def test_a_watch_blocked_by_the_engine_tests_it_again_after_a_minute(rig, tmp_path):
    folder = make_session(rig.root)
    flag = _breakable(tmp_path)
    rig.configure("--fail-while", str(flag), "--say", "x")
    since = loop.iso(T0)
    st, report = loop.run_pass(rig.deps, state.SttState(), since)
    assert report.state == "blocked"
    (flag / "broken").unlink()
    rig.clock.t = T0 + 30
    st, report = loop.run_pass(rig.deps, st, since)
    assert report.state == "blocked" and len(rig.runs()) == 1
    rig.clock.t = T0 + 61
    st, report = loop.run_pass(rig.deps, st, since)
    assert (report.state, report.done) == ("ok", 1) and (folder / "result.json").exists() and st.blocked is None


def test_an_unreadable_recording_does_not_stop_the_others(rig):
    bad = make_session(rig.root, "3QZK1M7-00042-0A80", wall_ms=1792620900000)
    good = make_session(rig.root, "3QZK1M7-00042-0A81", wall_ms=1792620000000)
    (bad / "seg-000.wav").chmod(0)
    try:
        rig.configure("--say", "x")
        report = rig.once()
        assert (report.state, report.done, report.waiting) == ("ok", 1, 1) and (good / "result.json").exists()
        errors = [json.loads(x) for x in rig.paths.log.read_text().splitlines() if '"io_error"' in x]
        assert [(e["session"], e["errno"]) for e in errors] == [("3QZK1M7-00042-0A80", 13)]
        assert str(bad) not in rig.paths.log.read_text()
        for at in (30, 150, 630):  # the same back-off as a failing command, then failed / E_IO
            rig.clock.t = T0 + at
            rig.once()
        assert failed(bad) == {"code": "E_IO", "retryable": True}
    finally:
        (bad / "seg-000.wav").chmod(0o600)


def test_an_unwritable_folder_is_skipped_too(rig):
    bad = make_session(rig.root, "3QZK1M7-00042-0A82", wall_ms=1792620900000)
    good = make_session(rig.root, "3QZK1M7-00042-0A83", wall_ms=1792620000000)
    bad.chmod(0o500)
    try:
        rig.configure("--say", "x")
        assert rig.once().done == 1 and (good / "result.json").exists()
        rig.clock.t = T0 + 630
        for at in (30, 150, 630):
            rig.clock.t = T0 + at
            assert rig.once().state == "ok"
    finally:
        bad.chmod(0o700)
    assert not (bad / "result.json").exists()


def test_a_recording_waiting_to_retry_says_queued(rig):
    folder = make_session(rig.root)
    rig.configure("--fail-unless-silent")
    rig.once()
    status = read_json(folder / "status.json")
    assert (status["state"], status["by"]) == ("queued", "cpa-stt") and contract.check_status(status).ok


def test_the_log_rotates_at_one_mib(tmp_path):
    log = tmp_path / "log" / "cpa-stt.log"
    log.parent.mkdir()
    log.write_bytes(b"x" * (1 << 20))
    loop.log(log, ev="selftest", ok=True)
    assert (log.parent / "cpa-stt.log.1").stat().st_size == 1 << 20
    assert json.loads(log.read_text())["ev"] == "selftest"


def test_an_idle_watch_does_not_rewrite_its_state(rig, monkeypatch):
    rig.configure("--say", "x")
    saves, reports = [], []
    real = state.save
    monkeypatch.setattr(state, "save", lambda path, s: saves.append(s) or real(path, s))
    loop.watch(rig.deps, interval=0, stop=lambda: len(reports) >= 3, sleep=lambda s: None, report=reports.append)
    assert len(reports) == 3 and saves == []


def test_a_failing_file_backs_off_then_fails_retryable(rig):
    folder = make_session(rig.root)
    rig.configure("--fail-unless-silent")  # the silent self-test passes, the recording fails
    steps = [(0, 3), (29, 3), (30, 5), (150, 7), (629, 7)]  # (seconds after T0, command calls so far)
    for at, calls in steps:
        rig.clock.t = T0 + at
        rig.once()
        assert not (folder / "result.json").exists() and len(rig.runs()) == calls, at
    rig.clock.t = T0 + 630
    rig.once()
    assert failed(folder) == {"code": "E_STT", "retryable": True}
    assert len(rig.runs()) == 9 and state.load(rig.paths.state).failures == {}


def test_a_broken_command_blocks_without_results_until_the_config_changes(rig):
    folder = make_session(rig.root)
    rig.configure("--exit", "1")
    report = rig.once()
    assert (report.state, report.reason) == ("blocked", "engine")
    status = read_json(folder / "status.json")
    assert (status["state"], status["reason"], status["by"]) == ("blocked", "engine", "cpa-stt")
    assert contract.check_status(status).ok and not (folder / "result.json").exists()
    assert state.load(rig.paths.state).blocked == "engine"
    rig.configure("--say", "fixed")
    assert rig.once().done == 1 and read_json(folder / "result.json")["memo"]["text"] == "fixed"
    assert state.load(rig.paths.state).blocked is None


def test_a_missing_model_blocks_as_model_missing(rig, tmp_path):
    folder = make_session(rig.root)
    rig.configure("--prompt", str(tmp_path / "gone" / "model.bin"))
    report = rig.once()
    assert (report.state, report.reason) == ("blocked", "model_missing")
    assert read_json(folder / "status.json")["reason"] == "model_missing" and rig.runs() == []


def test_without_sandbox_exec_nothing_runs(rig, tmp_path):
    folder = make_session(rig.root)
    rig.configure("--say", "x")
    gone = runner.Env(str(tmp_path / "no-sandbox-exec"), None, (), str(tmp_path))
    report = loop.once(loop.Deps(rig.paths, rig.deps.notes_dir, gone, rig.clock, os.getpid(), lambda pid: True))
    assert (report.state, report.reason) == ("blocked", "engine")
    assert rig.runs() == [] and not (folder / "result.json").exists()


def test_a_vanished_sandbox_during_a_run_is_a_failed_run_not_a_crash(rig, tmp_path):
    folder = make_session(rig.root)
    doc = read_json(folder / "manifest.json")
    gone = runner.Env(str(tmp_path / "no-sandbox-exec"), None, (), str(tmp_path))
    ctx = process.Ctx(config.parse_config({"provider": "command", "argv": fake_argv("--say", "x")}), gone,
                      now=lambda: loop.iso(T0), tick=lambda: True)  # fmt: skip
    heard = process.hear(folder, manifest.runs(doc)[0], ctx)
    assert heard.failed is not None and not heard.failed.ok and heard.sentences == ()
