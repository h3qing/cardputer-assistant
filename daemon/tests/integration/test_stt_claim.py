"""One transcriber at a time (docs/22 §4.10): the recordings/.transcriber claim, losing it or stopping mid-run,
second writers and their leftovers."""

from __future__ import annotations

import json
import os
import time
from dataclasses import replace

import pytest

from cpa_stt import claim, config, loop, process, runner, state
from tests.support.stt_rig import T0, make_rig, put_claim
from tests.support.stt_tree import SID, make_session, read_json


@pytest.fixture
def rig(tmp_path):
    return make_rig(tmp_path)


def _hijack_run(monkeypatch, before_tick):
    """runner.run whose tick first calls before_tick() for a recording (not the self-test)."""
    real = runner.run

    def run(argv, wav, offset_ms, timeout_s, env, wd, *, tick, **kw):
        def hooked():
            if wav.name != "selftest.wav":
                before_tick()
            return tick()

        return real(argv, wav, offset_ms, timeout_s, env, wd, tick=hooked, **kw)

    monkeypatch.setattr(runner, "run", run)


def _age(folder, *names, seconds):
    for name in names:
        os.utime(folder / name, (T0 - seconds, T0 - seconds))


def test_another_transcribers_folder_is_left_alone_and_reported(rig):
    folder = make_session(rig.root)
    status = {"v": 1, "session": SID, "attempt": 1, "state": "transcribing", "at": "2026-10-21T14:45:12Z"}
    (folder / "status.json").write_text(json.dumps(status))
    rig.configure("--say", "x")
    report = rig.once()
    assert report.foreign == (f"2026-10-21/{folder.name}",)
    assert sorted(p.name for p in folder.iterdir()) == ["manifest.json", "seg-000.wav", "status.json"]
    assert state.load(rig.paths.state).foreign == report.foreign


def test_our_own_status_from_before_a_restart_is_ours(rig):
    folder = make_session(rig.root)
    (folder / "status.json").write_text(json.dumps({"v": 1, "state": "transcribing", "by": "cpa-stt"}))
    rig.configure("--say", "x")
    assert rig.once().done == 1 and (folder / "result.json").exists()


def test_a_fresh_claim_by_someone_else_means_hands_off(rig):
    folder = make_session(rig.root)
    put_claim(rig.root, "whisperwoof", 60)
    rig.configure("--say", "x")
    report = rig.once()
    assert (report.state, report.by) == ("busy", "whisperwoof")
    assert sorted(p.name for p in folder.iterdir()) == ["manifest.json", "seg-000.wav"] and rig.runs() == []
    assert json.loads((rig.root / ".transcriber").read_text())["app"] == "whisperwoof"


def test_a_stale_claim_is_taken_over_and_released_after_once(rig):
    folder = make_session(rig.root)
    put_claim(rig.root, "whisperwoof", 301)
    rig.configure("--say", "x")
    assert rig.once().done == 1 and (folder / "result.json").exists()
    assert not (rig.root / ".transcriber").exists()


def test_heartbeat_renews_only_an_old_beat(tmp_path):
    claim.write(tmp_path, 42, since=loop.iso(T0), now=loop.iso(T0))
    os.utime(tmp_path / ".transcriber", (T0, T0))
    loop.heartbeat(tmp_path, 42, loop.iso(T0), T0 + 30)
    assert claim.read(tmp_path).beat == pytest.approx(T0, abs=1)
    loop.heartbeat(tmp_path, 42, loop.iso(T0), T0 + 61)
    assert claim.read(tmp_path).beat == pytest.approx(T0 + 61, abs=1)


def test_a_claim_taken_midway_stops_the_pass(rig, monkeypatch):
    first = make_session(rig.root, "3QZK1M7-00042-0A70", wall_ms=1792620900000)
    second = make_session(rig.root, "3QZK1M7-00042-0A71", wall_ms=1792620000000)
    real = process.process

    def then_someone_claims(folder, ctx):
        step = real(folder, ctx)
        put_claim(rig.root, "whisperwoof", 0)
        return step

    monkeypatch.setattr(process, "process", then_someone_claims)
    rig.configure("--say", "x")
    report = rig.once()
    assert (report.state, report.by, report.done) == ("busy", "whisperwoof", 1)
    assert (first / "result.json").exists() and not (second / "status.json").exists()
    assert json.loads((rig.root / ".transcriber").read_text())["app"] == "whisperwoof"


def test_losing_the_claim_during_a_run_kills_it_and_writes_nothing_more(rig, monkeypatch):
    folder = make_session(rig.root)
    _hijack_run(monkeypatch, lambda: put_claim(rig.root, "whisperwoof", 0))
    rig.configure("--sleep-loud", "30", "--say", "x")
    start = time.monotonic()
    report = rig.once()
    assert (report.state, report.by, report.done) == ("busy", "whisperwoof", 0) and time.monotonic() - start < 10
    assert not (folder / "transcript.json").exists() and not (folder / "result.json").exists()
    assert json.loads((rig.root / ".transcriber").read_text())["app"] == "whisperwoof"


def test_losing_the_claim_after_the_run_writes_no_transcript(rig, monkeypatch):
    folder = make_session(rig.root)
    real = process.hear

    def heard_then_claimed(path, run, ctx):
        got = real(path, run, ctx)
        put_claim(rig.root, "whisperwoof", 0)
        return got

    monkeypatch.setattr(process, "hear", heard_then_claimed)
    rig.configure("--say", "x")
    assert rig.once().state == "busy"
    assert not (folder / "transcript.json").exists() and not (folder / "result.json").exists()


def test_a_pass_with_nothing_to_do_still_renews_the_claim(rig):
    rig.configure("--say", "x")
    since = loop.iso(T0)
    st, report = loop.run_pass(rig.deps, state.SttState(), since)
    assert report.state == "ok" and claim.read(rig.root).beat == pytest.approx(T0, abs=1)
    rig.clock.t = T0 + 61
    loop.run_pass(rig.deps, st, since)
    assert claim.read(rig.root).beat == pytest.approx(T0 + 61, abs=1)


def test_a_claim_created_meanwhile_by_another_taker_means_busy(rig, monkeypatch):
    make_session(rig.root)
    rig.configure("--say", "x")
    real = claim.take

    def raced(root, pid, **kw):
        put_claim(root, "whisperwoof", 0)
        return real(root, pid, **kw)

    monkeypatch.setattr(claim, "take", raced)
    report = rig.once()
    assert (report.state, report.by) == ("busy", "whisperwoof") and rig.runs() == []


def test_stop_ends_the_pass_before_the_next_folder(rig):
    first = make_session(rig.root, "3QZK1M7-00042-0A74", wall_ms=1792620900000)
    second = make_session(rig.root, "3QZK1M7-00042-0A75", wall_ms=1792620000000)
    rig.configure("--say", "x")
    report = loop.once(replace(rig.deps, stop=lambda: (first / "result.json").exists()))
    assert (report.state, report.done) == ("stopped", 1) and not (second / "status.json").exists()
    assert not (rig.root / ".transcriber").exists()


def test_turning_it_off_mid_pass_stops_before_the_next_folder(rig, monkeypatch):
    first = make_session(rig.root, "3QZK1M7-00042-0A76", wall_ms=1792620900000)
    second = make_session(rig.root, "3QZK1M7-00042-0A77", wall_ms=1792620000000)
    real = process.process

    def then_off(folder, ctx):
        step = real(folder, ctx)
        config.save_config(rig.paths.config, replace(config.load_config(rig.paths.config), provider="off"))
        return step

    monkeypatch.setattr(process, "process", then_off)
    rig.configure("--say", "x")
    report = rig.once()
    assert (report.state, report.done) == ("off", 1) and (first / "result.json").exists()
    assert not (second / "status.json").exists()


def test_stop_kills_a_running_command_and_writes_no_transcript(rig):
    folder = make_session(rig.root)
    rig.configure("--sleep-loud", "30", "--say", "x")
    start = time.monotonic()
    report = loop.once(replace(rig.deps, stop=lambda: time.monotonic() - start > 0.5))
    assert report.state == "stopped" and time.monotonic() - start < 10
    assert not (folder / "transcript.json").exists() and not (folder / "result.json").exists()


def test_another_programs_leftovers_are_taken_over_after_five_minutes(rig):
    folder = make_session(rig.root)
    (folder / "status.json").write_text(json.dumps({"v": 1, "state": "transcribing", "by": "whisperwoof"}))
    (folder / "transcript.json").write_text("{}")
    _age(folder, "status.json", "transcript.json", seconds=301)
    rig.configure("--say", "x")
    assert rig.once().done == 1 and read_json(folder / "status.json")["by"] == "cpa-stt"
    assert read_json(folder / "transcript.json")["sentences"][0]["text"] == "x"


def test_another_programs_files_from_the_last_five_minutes_mean_hands_off(rig):
    folder = make_session(rig.root)
    (folder / "transcript.md").write_text("theirs")
    _age(folder, "transcript.md", seconds=60)
    rig.configure("--say", "x")
    assert rig.once().foreign == (f"2026-10-21/{folder.name}",)
    assert (folder / "transcript.md").read_text() == "theirs" and not (folder / "status.json").exists()


def test_a_status_another_program_writes_during_the_run_stops_the_writes(rig, monkeypatch):
    folder = make_session(rig.root)
    real = process.hear

    def heard_then_overwritten(path, run, ctx):
        got = real(path, run, ctx)
        (path / "status.json").write_text(json.dumps({"v": 1, "state": "transcribing", "by": "whisperwoof"}))
        return got

    monkeypatch.setattr(process, "hear", heard_then_overwritten)
    rig.configure("--say", "x")
    report = rig.once()
    assert report.foreign == (f"2026-10-21/{folder.name}",) and report.done == 0
    assert not (folder / "transcript.json").exists() and not (folder / "result.json").exists()


def test_results_another_program_wrote_since_the_claim_are_reported(rig):
    today = loop.iso(T0)[:10]
    pipelines = {"0A90": ("WhisperWoof/2.9.0", today), "0A91": ("cpa-stt", today), "0A92": ("WhisperWoof/2.9.0",
                 "2026-10-20")}  # fmt: skip
    folders = {}
    for tail, (app, day) in pipelines.items():
        folders[tail] = make_session(rig.root, f"3QZK1M7-00042-{tail}", date=day)
        (folders[tail] / "result.json").write_text(json.dumps({"state": "done", "pipeline": {"app": app}}))
    rig.configure("--say", "x")
    report = rig.once()
    assert report.foreign == (f"{today}/{folders['0A90'].name}",)
    assert state.load(rig.paths.state).foreign == report.foreign


def test_a_foreign_folder_is_logged_once(rig):
    folder = make_session(rig.root)
    (folder / "status.json").write_text(json.dumps({"v": 1, "state": "transcribing"}))
    rig.configure("--say", "x")
    rig.once()
    rig.once()
    assert sum('"foreign"' in x for x in rig.paths.log.read_text().splitlines()) == 1


def test_a_claim_that_cannot_be_renewed_counts_as_lost(tmp_path, monkeypatch):
    """Others would take it over after 5 minutes, so cpa-stt stops writing instead of failing mid-run."""
    claim.write(tmp_path, 42, since=loop.iso(T0), now=loop.iso(T0))

    def read_only(*args, **kwargs):
        raise PermissionError(13, "read-only")

    monkeypatch.setattr(claim, "write", read_only)
    assert loop.heartbeat(tmp_path, 42, loop.iso(T0), T0 + 30)
    assert not loop.heartbeat(tmp_path, 42, loop.iso(T0), T0 + 61)
