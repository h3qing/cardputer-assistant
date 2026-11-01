"""The cpa command line (05 §7.9; US-05, US-08 AC4, US-15). Runs against a temporary $CPA_HOME."""

from __future__ import annotations

import asyncio
import json
import os
import plistlib
import signal
import subprocess
import sys
import time

import pytest

from cpad.cli.main import CliDeps
from cpad.cli.main import main as cpa
from cpad.config.files import write_json
from cpad.control import client
from cpad.control.server import API
from cpad.service import launchd
from tests.support.fake_launchd import FakeLaunchd


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "home"
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


def _out(capsys) -> str:
    return capsys.readouterr().out


def test_project_add_ls_rm(home, tmp_path, capsys):
    root = tmp_path / "proj"
    root.mkdir()
    assert cpa(["project", "add", "cpa", str(root), "--slot", "1", "--name", "Cardputer 助手"]) == 0
    assert cpa(["project", "add", "blog", str(root), "--slot", "2", "--mode", "acceptEdits"]) == 0
    capsys.readouterr()
    assert cpa(["project", "ls"]) == 0
    listing = _out(capsys)
    assert "cpa" in listing and "Cardputer 助手" in listing and "acceptEdits" in listing and "plan" in listing
    assert oct((home / "projects.json").stat().st_mode & 0o777) == "0o600"
    assert oct(home.stat().st_mode & 0o777) == "0o700"
    assert cpa(["project", "rm", "blog"]) == 0
    assert [p["key"] for p in json.loads((home / "projects.json").read_text())["projects"]] == ["cpa"]


@pytest.mark.parametrize(
    "args",
    [
        ["project", "add", "Bad", "{root}", "--slot", "1"],
        ["project", "add", "ok", "{root}", "--slot", "10"],
        ["project", "add", "ok", "{root}/missing", "--slot", "2"],
        ["project", "add", "ok", "{root}", "--slot", "2", "--mode", "bypassPermissions"],
        ["project", "add", "ok", "{root}", "--slot", "2", "--name", "名" * 9],
        ["project", "add", "taken", "{root}", "--slot", "1"],
        ["project", "rm", "nope"],
    ],
)
def test_project_errors_write_nothing(home, tmp_path, capsys, args):
    root = tmp_path / "proj"
    root.mkdir()
    assert cpa(["project", "add", "first", str(root), "--slot", "1"]) == 0
    before = (home / "projects.json").read_bytes()
    assert cpa([a.replace("{root}", str(root)) for a in args]) != 0
    assert (home / "projects.json").read_bytes() == before
    assert "error" in capsys.readouterr().err


def test_queue_add_ls_done(home, tmp_path, capsys):
    root = tmp_path / "proj"
    root.mkdir()
    cpa(["project", "add", "cpa", str(root), "--slot", "1"])
    capsys.readouterr()
    assert cpa(["queue", "add", "cpa", "refactor", "the\toutbox"]) == 0
    assert _out(capsys).strip() == "q1"
    assert cpa(["queue", "add", "nope", "x"]) != 0
    assert cpa(["queue", "add", "cpa", "   "]) != 0
    assert cpa(["queue", "ls", "cpa"]) == 0
    assert "q1" in (listing := _out(capsys)) and "refactor the outbox" in listing
    assert cpa(["queue", "done", "q1"]) == 0
    assert cpa(["queue", "done", "q1"]) != 0
    capsys.readouterr()
    cpa(["queue", "ls"])
    assert "q1" not in _out(capsys)
    events = [json.loads(line) for line in (home / "events.jsonl").read_text().splitlines()]
    assert [e["ev"] for e in events] == ["queue.added", "queue.done"] and events[0]["origin"] == "cli"


def test_queue_reopen_undoes_a_done(home, tmp_path, capsys):
    """docs/20 U12: Opt+D on the wrong row can be undone on the Mac."""
    root = tmp_path / "proj"
    root.mkdir()
    cpa(["project", "add", "cpa", str(root), "--slot", "1"])
    cpa(["queue", "add", "cpa", "ask", "lawyer"])
    assert cpa(["queue", "reopen", "q1"]) != 0  # still open
    assert cpa(["queue", "reopen", "q7"]) != 0  # never existed
    assert cpa(["queue", "done", "q1"]) == 0
    capsys.readouterr()
    assert cpa(["queue", "reopen", "q1"]) == 0
    assert _out(capsys).strip() == "reopened q1: ask lawyer"
    cpa(["queue", "ls", "cpa"])
    assert "q1" in _out(capsys)
    events = [json.loads(line) for line in (home / "events.jsonl").read_text().splitlines()]
    assert [e["ev"] for e in events] == ["queue.added", "queue.done", "queue.reopened"]
    # the record carries the item (the fold keeps no closed items: review, linear fold)
    assert {k: events[-1][k] for k in ("qid", "project", "text", "intent")} == {
        "qid": "q1",
        "project": "cpa",
        "text": "ask lawyer",
        "intent": "todo",
    }


def test_status_when_not_running_shows_no_bodies(home, tmp_path, capsys):
    root = tmp_path / "proj"
    root.mkdir()
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"notes_dir": str(tmp_path / "notes")})  # never the user's claim (#71)
    cpa(["project", "add", "cpa", str(root), "--slot", "1"])
    cpa(["queue", "add", "cpa", "a very private plan"])
    capsys.readouterr()
    assert cpa(["status"]) == 0
    text = _out(capsys)
    assert "not running" in text and "cpa" in text and "private" not in text
    assert "transcriber: none running" in text


def test_status_names_the_transcriber_that_claims_recordings(home, tmp_path, capsys):
    notes = tmp_path / "notes"
    (notes / "recordings").mkdir(parents=True)
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"notes_dir": str(notes)})
    beat = time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime())
    beat = beat[:-2] + ":" + beat[-2:]
    claim = {"v": 1, "app": "cpa-stt", "pid": 4242, "since": beat, "beat_at": beat}
    (notes / "recordings" / ".transcriber").write_text(json.dumps(claim))
    assert cpa(["status"]) == 0
    assert "transcriber: cpa-stt pid 4242, beat " in _out(capsys)
    assert cpa(["status", "--json"]) == 0
    assert json.loads(_out(capsys))["transcriber"]["app"] == "cpa-stt"


def test_status_against_a_running_daemon_survives_a_broken_config(home, capsys, monkeypatch):
    from cpad.control import client  # noqa: PLC0415

    home.mkdir(mode=0o700)
    (home / "config.json").write_text("{", encoding="utf-8")
    monkeypatch.setattr(client, "request", lambda sock, msg: {"running": True, "version": "0.4.1"})
    assert cpa(["status", "--json"]) == 0
    assert json.loads(_out(capsys))["transcriber"] == {"app": None, "pid": None, "beat_age_s": None, "fresh": False}


@pytest.mark.parametrize("api", [None, 0, API - 1])
def test_status_tells_to_restart_a_cpad_older_than_cpa(home, capsys, monkeypatch, api):
    """docs/23 §1.3: a cpad started before an update answers without (or with a lower) api."""
    doc = {"ok": True, "running": True, "version": "0.1.0", "link": "offline", "device": {}, "warnings": [],
           "agent": {"status": "idle", "version": None, "spawn": True, "reply": False}}  # fmt: skip
    monkeypatch.setattr(client, "request", lambda path, payload: doc if api is None else {**doc, "api": api})
    assert cpa(["status"]) == 0
    assert "warning: cpad is older than this cpa; restart it: cpa service install" in _out(capsys)
    assert cpa(["status", "--json"]) == 0
    assert json.loads(_out(capsys))["warnings"] == ["cpad is older than this cpa; restart it: cpa service install"]
    assert doc["warnings"] == [], "the reply is not changed in place"


def test_pause_without_a_daemon_is_harmless(home, capsys):
    assert cpa(["pause", "120"]) == 0
    assert "not running" in _out(capsys)


def test_device_forget(home, capsys):
    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"device": {"serial": "02:00"}})
    assert cpa(["device", "forget"]) == 0
    assert json.loads((home / "config.json").read_text())["device"]["serial"] is None


def test_service_install_and_uninstall(home, tmp_path, capsys):
    fake = FakeLaunchd(login_env="PATH=/opt/bin:/usr/bin\nHOME=/Users/u\nSECRET_TOKEN=nope\n")
    calls = fake.calls
    agents = tmp_path / "LaunchAgents"
    deps = CliDeps(runner=fake, agents_dir=agents, uid=501, which=lambda name, path=None: "/opt/bin/claude",
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 0
    assert "cpad running: pid 4000" in _out(capsys)
    cfg = json.loads((home / "config.json").read_text())
    assert (
        cfg["spawn_env"] == {"PATH": "/opt/bin:/usr/bin", "HOME": "/Users/u"} and cfg["claude_bin"] == "/opt/bin/claude"
    )
    plist = plistlib.loads((agents / f"{launchd.LABEL}.plist").read_bytes())
    assert plist["ProgramArguments"][-3:] == ["-m", "cpad", "daemon"]
    assert ["launchctl", "bootstrap", "gui/501", str(agents / f"{launchd.LABEL}.plist")] in calls
    assert cpa(["service", "uninstall"], deps) == 0
    assert not (agents / f"{launchd.LABEL}.plist").exists()


def test_service_install_gives_cpad_the_login_language(home, tmp_path):
    fake = FakeLaunchd(login_env="PATH=/opt/bin\nLANG=zh_CN.UTF-8\nLC_ALL=\nLC_MESSAGES=zh_CN\nTOKEN=x\n")
    deps = CliDeps(runner=fake, agents_dir=tmp_path / "A", uid=501, which=lambda name, path=None: None,
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 0
    plist = plistlib.loads((tmp_path / "A" / f"{launchd.LABEL}.plist").read_bytes())
    assert plist["EnvironmentVariables"] == {"CPA_HOME": str(home), "LANG": "zh_CN.UTF-8", "LC_MESSAGES": "zh_CN"}


def test_service_install_keeps_the_rest_of_config_json_as_written(home, tmp_path):
    """docs/27 §2.2: re-running the installer must not rewrite the user's config. Only spawn_env and claude_bin
    change; every other key keeps its text (no defaults filled in, 5 stays 5, the order stays)."""
    home.mkdir(mode=0o700)
    mine = '{"rec_pull": false, "notes_dir": "~/N", "device": {"serial": "02:00"}, "poll_s": 5, "host_label": "书房"}'
    (home / "config.json").write_text(mine, encoding="utf-8")

    fake = FakeLaunchd(login_env="PATH=/opt/bin\n")
    deps = CliDeps(runner=fake, agents_dir=tmp_path / "A", uid=501, which=lambda name, path=None: "/opt/bin/claude",
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 0
    after = json.loads((home / "config.json").read_text(encoding="utf-8"))
    assert {k: v for k, v in after.items() if k not in ("spawn_env", "claude_bin")} == json.loads(mine)
    assert list(after)[:5] == list(json.loads(mine))
    assert after["spawn_env"] == {"PATH": "/opt/bin"} and after["claude_bin"] == "/opt/bin/claude"
    assert (home / "config.json").stat().st_mode & 0o777 == 0o600


def test_service_install_upgrade_waits_for_the_old_cpad_and_verifies_the_new_one(home, tmp_path, capsys):
    """#82: the old job's bootout is slow; the new pid is checked through launchctl print."""
    fake = FakeLaunchd(loaded=True, pid=1234, bootout_delay=2.5)
    deps = CliDeps(runner=fake, agents_dir=tmp_path / "A", uid=501, which=lambda name, path=None: "/opt/bin/claude",
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 0
    assert fake.loaded and fake.pid == 4000 and fake.verbs("bootstrap") == 1
    assert "cpad running: pid 4000" in _out(capsys)


def test_service_install_says_so_when_cpad_did_not_start(home, tmp_path, capsys, monkeypatch):
    fake = FakeLaunchd(loaded=True, pid=1234, dead_starts=2)
    deps = CliDeps(runner=fake, agents_dir=tmp_path / "A", uid=501, which=lambda name, path=None: "/opt/bin/claude",
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 1
    err = capsys.readouterr().err
    assert "cpad did not start" in err and "try again: cpa service install" in err and "后台服务" not in err
    assert fake.verbs("bootstrap") == 2
    monkeypatch.setenv("CPA_LANG", "zh")
    again = FakeLaunchd(loaded=True, pid=1234, dead_starts=2)
    assert cpa(["service", "install"], CliDeps(runner=again, agents_dir=tmp_path / "A", uid=501,
                                               which=lambda name, path=None: None, clock=again.clock)) == 1  # fmt: skip
    assert "后台服务没起来，再跑一次 cpa service install" in capsys.readouterr().err


def test_service_install_warns_in_one_language_when_claude_is_missing(home, tmp_path, capsys, monkeypatch):
    fake = FakeLaunchd()
    deps = CliDeps(runner=fake, agents_dir=tmp_path / "A", uid=501, which=lambda name, path=None: None,
                   clock=fake.clock)  # fmt: skip
    assert cpa(["service", "install"], deps) == 0
    err = capsys.readouterr().err
    assert "warning: claude was not found on the login PATH; spawning stays off" in err and "没找到" not in err
    monkeypatch.setenv("CPA_LANG", "zh")
    assert cpa(["service", "install"], deps) == 0
    err = capsys.readouterr().err
    assert "没找到 Claude Code：装好以后再跑一次 cpa service install" in err and "spawning" not in err


def test_service_install_refuses_a_broken_config_json(home, tmp_path):
    home.mkdir(mode=0o700)
    (home / "config.json").write_text('{"poll_s": "fast"}', encoding="utf-8")
    deps = CliDeps(runner=lambda argv, **kw: launchd.Completed(0, ""), agents_dir=tmp_path / "A", uid=501,
                   which=lambda name, path=None: None)  # fmt: skip
    assert cpa(["service", "install"], deps) == 2
    assert (home / "config.json").read_text(encoding="utf-8") == '{"poll_s": "fast"}'
    assert not (tmp_path / "A").exists()


async def test_status_and_pause_against_a_running_daemon(home, tmp_path, capsys):
    from cpad.app import Daemon
    from cpad.config.settings import Paths

    home.mkdir(mode=0o700)
    write_json(home / "config.json", {"claude_bin": "/nonexistent/claude", "notes_dir": str(tmp_path / "notes")})
    stop = asyncio.Event()
    task = asyncio.create_task(Daemon(Paths.at(home), port_lister=lambda: ()).run(stop))
    try:
        for _ in range(100):
            if (home / "cpad.sock").exists():
                break
            await asyncio.sleep(0.02)
        assert await asyncio.to_thread(cpa, ["status", "--json"]) == 0
        status = json.loads(_out(capsys))
        assert status["running"] is True and status["link"] in ("offline", "searching")
        assert status["api"] == API and not any("older" in w for w in status["warnings"])
        assert await asyncio.to_thread(cpa, ["pause", "--seconds", "5"]) == 0
        assert "paused" in _out(capsys)
    finally:
        stop.set()
        await asyncio.wait_for(task, 5)


def test_daemon_entry_point_runs_and_stops_on_sigterm(home):
    home.mkdir(mode=0o700)
    write_json(
        home / "config.json", {"device": {"serial": "no-such-serial-for-tests"}, "claude_bin": "/nonexistent/claude"}
    )
    env = {**os.environ, "CPA_HOME": str(home)}
    proc = subprocess.Popen([sys.executable, "-m", "cpad", "daemon"], env=env, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while not (home / "cpad.sock").exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert (home / "cpad.sock").exists()
        second = subprocess.run([sys.executable, "-m", "cpad", "daemon"], env=env, capture_output=True, timeout=10)
        assert second.returncode != 0 and b"already running" in second.stderr
        proc.send_signal(signal.SIGTERM)
        assert proc.wait(timeout=10) == 0
    finally:
        if proc.poll() is None:
            proc.kill()
    assert not (home / "cpad.sock").exists()
