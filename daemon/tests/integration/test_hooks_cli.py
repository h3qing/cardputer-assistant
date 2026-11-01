"""`cpa hook` (stdin -> cpad.sock) and `cpa hooks print|install|uninstall` (#33).

HOME and CPA_HOME are temporary in every test; nothing here can reach the real ~/.claude.
"""

from __future__ import annotations

import asyncio
import io
import json
import os
import subprocess
import sys
import time

import pytest

from cpad.agents.claude_code import hook_settings
from cpad.agents.claude_code.hooks import STDIN_MAX, HookEvent
from cpad.cli.main import main as cpa
from cpad.config.settings import cpa_home
from cpad.control import server
from tests.support.fake_claude import tty_session
from tests.support.rig import until

SID = "8d3e7a19-4c2b-4a6e-9f10-2b7c5d8e3a61"
PERMISSION = {"session_id": SID, "hook_event_name": "PermissionRequest", "cwd": "/p", "tool_name": "Bash",
              "tool_input": {"command": "rm -rf build"}, "transcript_path": "/t.jsonl"}  # fmt: skip


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "home"
    path.mkdir(mode=0o700)
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


@pytest.fixture
def fake_home(tmp_path, monkeypatch):
    """HOME and CPA_HOME for `cpa hooks`: settings and backups land here, never in the real ~/.claude or
    the user's CPA_HOME."""
    monkeypatch.setenv("HOME", str(tmp_path / "user"))
    monkeypatch.setenv("CPA_HOME", str(tmp_path / "cpa"))
    monkeypatch.delenv("CLAUDE_CONFIG_DIR", raising=False)
    target = hook_settings.default_path()
    assert str(target).startswith(str(tmp_path)), target
    return target


@pytest.fixture
def cpa_exe(tmp_path):
    exe = tmp_path / "venv" / "bin" / "cpa"
    exe.parent.mkdir(parents=True)
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o755)
    return str(exe)


async def serve(path):
    seen = []

    async def handler(req):
        seen.append(req)
        return {"ok": True}

    srv = server.ControlServer(path, handler)
    await srv.start()
    return srv, seen


def run_hook(data: bytes, *args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, "-m", "cpad", "hook", *args], input=data, capture_output=True, timeout=20,
                          env={**os.environ, **(env or {})}, check=False)  # fmt: skip


def hook_in_process(monkeypatch, data: bytes, *args: str) -> int:
    monkeypatch.setattr(sys, "stdin", io.TextIOWrapper(io.BytesIO(data)))
    return cpa(["hook", *args])


# --- cpa hook -------------------------------------------------------------------------------


async def test_hook_forwards_one_compact_event_and_prints_nothing(home):
    srv, seen = await serve(home / "cpad.sock")
    try:
        done = await asyncio.to_thread(run_hook, json.dumps(PERMISSION).encode(), "PermissionRequest")
    finally:
        await srv.close()
    assert done.returncode == 0 and done.stdout == b"" and done.stderr == b""
    assert seen == [{"cmd": "hook", "event": HookEvent(ev="PermissionRequest", session_id=SID, tool="Bash")}]


def test_hook_succeeds_silently_when_cpad_is_down(home):
    started = time.monotonic()
    done = run_hook(json.dumps(PERMISSION).encode())
    assert done.returncode == 0 and done.stdout == b"" and done.stderr == b""
    assert time.monotonic() - started < 10


@pytest.mark.parametrize(
    ("data", "args"),
    [(b"", ()), (b"not json", ()), (b"[]", ()), (b'{"hook_event_name": "Stop"}', ()),
     (json.dumps(PERMISSION).encode(), ("Stop",)), (json.dumps(PERMISSION).encode(), ("--bogus", "--home")),
     (json.dumps(PERMISSION).encode(), ("--bogus",)), (json.dumps(PERMISSION).encode(), ("PermissionRequest", "x")),
     (b'{"session_id": "' + SID.encode() + b'", "hook_event_name": "Stop", "x": "' + b"a" * STDIN_MAX + b'"}', ())],
)  # fmt: skip
async def test_hook_drops_anything_it_cannot_trust(home, monkeypatch, capsys, data, args):
    srv, seen = await serve(home / "cpad.sock")
    try:
        rc = await asyncio.to_thread(hook_in_process, monkeypatch, data, *args)
    finally:
        await srv.close()
    out = capsys.readouterr()
    assert rc == 0 and out.out == "" and seen == []


async def test_hook_home_flag_picks_the_socket(home, short_tmp, monkeypatch):
    other = short_tmp / "other"
    other.mkdir()
    srv, seen = await serve(other / "cpad.sock")
    try:
        rc = await asyncio.to_thread(hook_in_process, monkeypatch, json.dumps(PERMISSION).encode(), "--home",
                                     str(other))  # fmt: skip
    finally:
        await srv.close()
    assert rc == 0 and len(seen) == 1


# --- cpa hooks ------------------------------------------------------------------------------


def test_hooks_print_shows_the_exact_snippet(cpa_exe, capsys):
    assert cpa(["hooks", "print", "--cpa", cpa_exe]) == 0
    assert json.loads(capsys.readouterr().out) == hook_settings.snippet(cpa_exe)


def test_hooks_print_names_a_custom_cpa_home(cpa_exe, home, capsys):
    assert cpa(["hooks", "print", "--cpa", cpa_exe]) == 0
    stop = json.loads(capsys.readouterr().out)["hooks"]["Stop"][0]["hooks"][0]
    assert stop["args"] == ["hook", "--home", str(home)]


def test_install_needs_yes_and_uninstall_restores(fake_home, cpa_exe, capsys, monkeypatch):
    monkeypatch.delenv("CPA_HOME", raising=False)
    fake_home.parent.mkdir(parents=True)
    fake_home.write_text('{"model": "opus"}\n')
    assert cpa(["hooks", "install", "--cpa", cpa_exe]) == 0
    assert "--yes" in capsys.readouterr().out and fake_home.read_text() == '{"model": "opus"}\n'
    assert cpa(["hooks", "install", "--cpa", cpa_exe, "--yes"]) == 0
    assert str(fake_home) in capsys.readouterr().out
    assert json.loads(fake_home.read_text()) == {"model": "opus", **hook_settings.snippet(cpa_exe)}
    backup = hook_settings.backup_path(cpa_home() / hook_settings.BACKUP_DIR, fake_home)
    assert str(backup).startswith(str(fake_home.parents[2])) and backup.read_text() == '{"model": "opus"}\n'
    assert [p.name for p in fake_home.parent.iterdir()] == ["settings.json"]
    assert cpa(["hooks", "install", "--cpa", cpa_exe, "--yes"]) == 0
    assert "already" in capsys.readouterr().out
    assert cpa(["hooks", "uninstall"]) == 0
    assert "hooks" in json.loads(fake_home.read_text())
    assert cpa(["hooks", "uninstall", "--yes"]) == 0
    assert json.loads(fake_home.read_text()) == {"model": "opus"}
    assert cpa(["hooks", "uninstall", "--yes"]) == 0
    assert "no cpa hooks" in capsys.readouterr().out


def test_install_into_an_explicit_file(tmp_path, cpa_exe, fake_home):
    target = tmp_path / "proj" / ".claude" / "settings.local.json"
    assert cpa(["hooks", "install", "--settings", str(target), "--cpa", cpa_exe, "--yes"]) == 0
    assert "hooks" in json.loads(target.read_text()) and not fake_home.exists()


def test_backups_of_a_project_file_stay_out_of_the_project(tmp_path, cpa_exe, fake_home, capsys):
    """settings.local.json is git-ignored by its exact name; a copy beside it would not be."""
    target = tmp_path / "proj" / ".claude" / "settings.local.json"
    target.parent.mkdir(parents=True)
    target.write_text('{"permissions": {"allow": ["Bash(curl -H token)"]}}\n')
    for argv in (["install", "--cpa", cpa_exe], ["uninstall"], ["install", "--cpa", cpa_exe]):
        assert cpa(["hooks", *argv, "--settings", str(target), "--yes"]) == 0
    assert [p.name for p in target.parent.iterdir()] == ["settings.local.json"]
    backups = tmp_path / "cpa" / hook_settings.BACKUP_DIR
    assert [p.name for p in backups.iterdir()] == [hook_settings.backup_path(backups, target).name]
    assert str(backups) in capsys.readouterr().out


def test_install_refuses_a_broken_settings_file(fake_home, cpa_exe, capsys):
    fake_home.parent.mkdir(parents=True)
    fake_home.write_text("{oops")
    assert cpa(["hooks", "install", "--cpa", cpa_exe, "--yes"]) == 2
    assert "error" in capsys.readouterr().err and fake_home.read_text() == "{oops"


def test_install_refuses_a_cpa_that_is_not_executable(fake_home, tmp_path, capsys):
    assert cpa(["hooks", "install", "--cpa", str(tmp_path / "nope" / "cpa"), "--yes"]) == 2
    assert not fake_home.exists()


# --- end to end with the daemon -------------------------------------------------------------


async def test_permission_hook_reaches_the_board_before_the_poll_does(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    rig.claude.update(sessions=[tty_session("8d3e7a19", "架构设计", rig.roots["cpa"], "busy")])
    await rig.synced()

    def row():
        return {s["sid"]: s for s in rig.fake.snapshots["sessions"]["sessions"]}.get("8d3e7a19", {})

    await until(lambda: row().get("st") == "busy")
    rc = await asyncio.to_thread(hook_in_process, monkeypatch, json.dumps(PERMISSION).encode(), "--home",
                                 str(rig.paths.home))  # fmt: skip
    assert rc == 0
    await until(lambda: row().get("st") == "waiting" and row().get("need") is True, timeout=2)
    assert rig.daemon.status()["hooks"]["sessions"] == 1
    rig.claude.update(sessions=[tty_session("8d3e7a19", "架构设计", rig.roots["cpa"], "waiting")])
    await asyncio.sleep(1.0)
    rig.claude.update(sessions=[tty_session("8d3e7a19", "架构设计", rig.roots["cpa"], "busy")])
    await until(lambda: row().get("st") == "busy" and row().get("need") is False, timeout=4)
