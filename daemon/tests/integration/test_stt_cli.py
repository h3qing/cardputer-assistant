"""The cpa-stt command line (docs/22 §4.11): use / off / status / test / retry / run --once / watch / service.
$CPA_HOME, the notes dir and every outside program (sandbox-exec, mdfind, launchctl) are fakes."""

from __future__ import annotations

import json
import plistlib
import sys

import pytest

from cpa_stt import cli, config
from tests.support import stt_tree
from tests.support.fake_launchd import FakeLaunchd
from tests.support.stt_home import DEAD_PID, Home
from tests.support.stt_tree import make_session, read_json


@pytest.fixture
def home(tmp_path, monkeypatch):
    return Home(tmp_path, monkeypatch)


def test_use_with_a_file_turns_it_on(home, capsys):
    assert home.main("use", str(home.config_file("--say", "x", timeout_s=60))) == 0
    cfg = config.load_config(home.paths.config)
    assert cfg.on and cfg.timeout_s == 60 and cfg.argv[-1] == "{wav}"
    out = capsys.readouterr().out
    assert "cpa-stt is on" in out and "cpa-stt run --once" in out and "cpa-stt service install" in out


@pytest.mark.parametrize("doc", [{"argv": ["relative", "{wav}"]}, {"argv": ["/bin/echo"]}, {"nope": 1}])
def test_use_refuses_bad_configs_and_saves_nothing(home, capsys, doc):
    path = home.tmp / "bad.json"
    path.write_text(json.dumps(doc))
    assert home.main("use", str(path)) == 2
    assert not home.paths.config.exists() and "error" in capsys.readouterr().err


def test_use_refuses_missing_paths(home, capsys):
    path = home.config_file("--prompt", str(home.tmp / "gone.bin"))
    assert home.main("use", str(path)) == 2
    assert "gone.bin" in capsys.readouterr().err and not home.paths.config.exists()


def test_use_keeps_the_old_config_when_the_self_test_fails(home, capsys):
    assert home.main("use", str(home.config_file("--exit", "1"))) == 1
    assert not home.paths.config.exists()
    assert "self-test failed" in capsys.readouterr().err


def test_off_turns_it_off_and_cleans_up(home, capsys):
    home.turn_on("--say", "x")
    at = "2026-10-22T10:00:00Z"
    claim = {"v": 1, "app": "cpa-stt", "pid": DEAD_PID, "since": at, "beat_at": at}
    (home.root / ".transcriber").write_text(json.dumps(claim))
    home.agents.mkdir()
    (home.agents / "com.cardputer-assistant.cpa-stt.plist").write_bytes(b"x")
    assert home.main("off") == 0
    assert not config.load_config(home.paths.config).on
    assert not (home.root / ".transcriber").exists()
    assert not (home.agents / "com.cardputer-assistant.cpa-stt.plist").exists()
    assert ["launchctl", "bootout", "gui/501/com.cardputer-assistant.cpa-stt"] in home.calls
    assert "cpa-stt is off" in capsys.readouterr().out


def test_off_leaves_a_live_or_foreign_claim(home):
    home.turn_on("--say", "x")
    claim = {"v": 1, "app": "whisperwoof", "pid": DEAD_PID, "since": "2026-10-22T10:00:00Z",
             "beat_at": "2026-10-22T10:00:00Z"}  # fmt: skip
    (home.root / ".transcriber").write_text(json.dumps(claim))
    assert home.main("off") == 0 and (home.root / ".transcriber").exists()


def test_run_once_processes_and_reports(home, capsys):
    folder = make_session(home.root)
    home.turn_on("--say", "SECRET-WORDS")
    assert home.main("run", "--once") == 0
    out = capsys.readouterr().out
    assert "done 1" in out and "SECRET-WORDS" not in out
    assert read_json(folder / "result.json")["memo"]["text"] == "SECRET-WORDS"


def test_run_once_when_off_or_blocked(home, capsys):
    make_session(home.root)
    assert home.main("run", "--once") == 0
    assert "cpa-stt is off" in capsys.readouterr().out
    home.turn_on("--exit", "1")
    assert home.main("run", "--once") == 1
    assert "blocked (engine)" in capsys.readouterr().out


def test_status_shows_health_but_no_text(home, capsys):
    make_session(home.root)
    home.turn_on("--say", "SECRET-WORDS")
    home.main("run", "--once")
    capsys.readouterr()
    assert home.main("status") == 0
    out = capsys.readouterr().out
    assert "cpa-stt: on" in out and "sandbox: ok" in out and "claim: none" in out and "self-test: ok" in out
    assert "recent: " in out and "SECRET-WORDS" not in out
    assert home.main("status", "--json") == 0
    doc = json.loads(capsys.readouterr().out)
    assert doc["on"] is True and doc["blocked"] is None and len(doc["recent"]) == 1


def test_status_warns_about_another_writer_and_a_missing_engine(home, capsys):
    folder = make_session(home.root)
    (folder / "status.json").write_text(json.dumps({"state": "transcribing"}))
    home.turn_on("--say", "x")
    home.main("run", "--once")
    capsys.readouterr()
    home.main("status", sandbox_exec=str(home.tmp / "gone"))
    out = capsys.readouterr().out
    assert "sandbox: MISSING" in out
    assert f"warning: another transcriber also writes recordings/: 2026-10-21/{folder.name}" in out


def test_status_when_off(home, capsys):
    assert home.main("status") == 0
    assert "cpa-stt: off" in capsys.readouterr().out


def test_test_prints_the_text_and_the_time(home, capsys):
    home.turn_on("--say", "hello there")
    wav = home.tmp / "mine.wav"
    wav.write_bytes(stt_tree.tone(16000))
    assert home.main("test", str(wav)) == 0
    out = capsys.readouterr().out
    assert "hello there" in out and "took" in out
    quiet = home.tmp / "quiet.wav"
    quiet.write_bytes(stt_tree.silence(16000))
    assert home.main("test", str(quiet)) == 0 and "nothing heard" in capsys.readouterr().out


def test_test_refuses_what_is_not_a_device_wav(home, capsys):
    home.turn_on("--say", "x")
    bad = home.tmp / "bad.wav"
    bad.write_bytes(b"RIFX" + stt_tree.tone(160)[4:])
    assert home.main("test", str(bad)) == 2
    assert home.main("test", str(home.tmp / "missing.wav")) == 2


def test_test_needs_a_command(home, capsys):
    wav = home.tmp / "mine.wav"
    wav.write_bytes(stt_tree.tone(1600))
    assert home.main("test", str(wav)) == 2
    assert "cpa-stt use" in capsys.readouterr().err


def test_retry(home, capsys):
    folder = make_session(home.root)
    (folder / "result.json").write_text(json.dumps({"state": "failed", "pipeline": {"app": "cpa-stt"}}))
    home.turn_on("--say", "x")
    assert home.main("retry") == 0
    assert "removed 1" in capsys.readouterr().out and not (folder / "result.json").exists()


def test_service_install_and_uninstall(home, capsys):
    assert home.main("service", "install") == 0
    plist = plistlib.loads((home.agents / "com.cardputer-assistant.cpa-stt.plist").read_bytes())
    assert plist["ProgramArguments"] == [sys.executable, "-m", "cpa_stt", "watch"]
    assert plist["EnvironmentVariables"] == {"CPA_HOME": str(home.home)} and plist["KeepAlive"] is True
    plist_path = home.agents / "com.cardputer-assistant.cpa-stt.plist"
    assert ["launchctl", "bootstrap", "gui/501", str(plist_path)] in home.calls
    assert home.main("service", "uninstall") == 0
    assert not (home.agents / "com.cardputer-assistant.cpa-stt.plist").exists()


def test_service_install_and_uninstall_boot_out_this_home_s_own_agent(home):
    ours = str(home.agents / "com.cardputer-assistant.cpa-stt.plist")
    fake = FakeLaunchd(loaded=True, pid=77, plist=ours)
    assert home.main("service", "install", launchctl=fake) == 0
    assert fake.verbs("bootout") == 1 and fake.verbs("bootstrap") == 1 and fake.loaded
    assert home.main("service", "uninstall", launchctl=fake) == 0
    assert fake.verbs("bootout") == 2 and not fake.loaded


def test_service_commands_leave_another_home_s_agent_alone(home, capsys):
    """The label is per user, not per HOME: from a throwaway HOME neither uninstall nor install touches it."""
    other = "/Users/u/Library/LaunchAgents/com.cardputer-assistant.cpa-stt.plist"
    fake = FakeLaunchd(loaded=True, pid=77, plist=other)
    home.agents.mkdir()
    (home.agents / "com.cardputer-assistant.cpa-stt.plist").write_bytes(b"x")
    assert home.main("service", "uninstall", launchctl=fake) == 0
    assert fake.verbs("bootout") == 0 and fake.pid == 77
    assert not (home.agents / "com.cardputer-assistant.cpa-stt.plist").exists()
    assert f"note: a cpa-stt LaunchAgent from another HOME is loaded ({other}): not touched" in capsys.readouterr().out
    assert home.main("service", "install", launchctl=fake) == 2
    assert fake.verbs("bootout") == 0 and fake.verbs("bootstrap") == 0 and fake.pid == 77
    assert not (home.agents / "com.cardputer-assistant.cpa-stt.plist").exists()
    assert "uninstall that one first" in capsys.readouterr().err


def test_service_install_gives_cpa_stt_this_terminal_s_language(home, monkeypatch):
    monkeypatch.setenv("LANG", "zh_CN.UTF-8")
    monkeypatch.setenv("CPA_LANG", "zh")
    assert home.main("service", "install") == 0
    plist = plistlib.loads((home.agents / "com.cardputer-assistant.cpa-stt.plist").read_bytes())
    assert plist["EnvironmentVariables"] == {"CPA_HOME": str(home.home), "CPA_LANG": "zh", "LANG": "zh_CN.UTF-8"}


def test_watch_runs_passes_until_stopped(home, capsys):
    folder = make_session(home.root)
    home.turn_on("--say", "x")
    assert home.main("watch", "--interval", "0", stop=lambda: (folder / "result.json").exists()) == 0
    assert (folder / "result.json").exists() and not (home.root / ".transcriber").exists()


def test_python_dash_m_runs_the_cli():
    import subprocess  # noqa: PLC0415

    done = subprocess.run([sys.executable, "-m", "cpa_stt", "--help"], capture_output=True, text=True, check=False)
    assert done.returncode == 0 and "cpa-stt" in done.stdout


def test_test_without_a_sandbox_says_why(home, capsys):
    home.turn_on("--say", "x")
    wav = home.tmp / "mine.wav"
    wav.write_bytes(stt_tree.tone(1600))
    assert home.main("test", str(wav), sandbox_exec=str(home.tmp / "gone")) == 2
    assert "engine" in capsys.readouterr().err


def test_off_works_even_with_a_broken_config(home, capsys):
    home.paths.dir.mkdir(parents=True)
    home.paths.config.write_text("{", encoding="utf-8")
    assert home.main("off") == 0
    assert config.load_config(home.paths.config).provider == "off"


def test_off_removes_the_launch_agent_before_it_drops_a_dead_claim(home, monkeypatch):
    from cpa_stt import claim, service  # noqa: PLC0415

    order = []
    monkeypatch.setattr(service, "uninstall", lambda *a, **kw: order.append("uninstall") or True)
    monkeypatch.setattr(claim, "release_dead", lambda *a, **kw: order.append("release_dead") or True)
    home.turn_on("--say", "x")
    assert home.main("off") == 0 and order == ["uninstall", "release_dead"]


def test_ctrl_c_stops_watch_and_a_second_one_ends_it_at_once():
    import signal  # noqa: PLC0415

    saved = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        stop, _ = cli.signal_stop()
        assert not stop()
        signal.getsignal(signal.SIGINT)(signal.SIGINT, None)
        assert stop() and signal.getsignal(signal.SIGINT) is signal.SIG_DFL
    finally:
        for sig, handler in saved.items():
            signal.signal(sig, handler)


def test_use_says_when_it_tested_and_that_only_one_transcriber_may_run(home, capsys):
    assert home.main("use", str(home.config_file("--say", "x"))) == 0
    out = capsys.readouterr().out
    assert "WhisperWoof" in out and "one transcriber" in out
    home.main("status")
    out = capsys.readouterr().out
    assert "self-test: ok (20" in out and "(None)" not in out


def test_status_says_when_nothing_is_running_and_whether_the_agent_is_installed(home, capsys):
    home.turn_on("--say", "x")
    home.main("status")
    out = capsys.readouterr().out
    assert "running: no, new recordings wait" in out and "service: not installed" in out
    home.main("service", "install")
    capsys.readouterr()
    home.main("status")
    assert "service: installed" in capsys.readouterr().out


def test_watch_says_what_it_does_but_never_the_text(home, capsys):
    folder = make_session(home.root)
    home.turn_on("--say", "SECRET-WORDS")
    assert home.main("watch", "--interval", "0", stop=lambda: (folder / "result.json").exists()) == 0
    out = capsys.readouterr().out
    assert "watching" in out and "Ctrl-C" in out
    assert f"transcribed {stt_tree.SID}: 1.0 s of audio in " in out and "SECRET-WORDS" not in out


def test_a_missing_model_names_the_file_and_how_to_relink_it(home, capsys):
    make_session(home.root)
    gone = home.tmp / "gone" / "ggml-silero-v5.1.2.bin"
    home.turn_on("--prompt", str(gone))
    assert home.main("run", "--once") == 1
    out = capsys.readouterr().out
    assert str(gone) in out and "cpa-stt setup" in out and "cpa-stt use" in out
