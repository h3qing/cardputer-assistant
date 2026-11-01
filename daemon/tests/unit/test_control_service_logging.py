"""Control socket (05 §7.9), LaunchAgent generation (US-15) and log redaction (§7.5, §7.9)."""

from __future__ import annotations

import asyncio
import json
import logging
import os
import plistlib
import stat

import pytest

from cpad.control import client, server
from cpad.logging import redact, setup
from cpad.service import launchd
from tests.support.fake_launchd import FakeLaunchd

# --- control --------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        (b'{"cmd":"status"}\n', {"cmd": "status"}),
        (b'{"cmd":"pause"}\n', {"cmd": "pause", "seconds": 120}),
        (b'{"cmd":"pause","seconds":600}\n', {"cmd": "pause", "seconds": 600}),
    ],
)
def test_parse_valid_requests(line, expected):
    assert server.parse_request(line) == expected


@pytest.mark.parametrize(
    "line",
    [b"nope\n", b"[]\n", b'{"cmd":"rm"}\n', b'{"cmd":"pause","seconds":0}\n', b'{"cmd":"pause","seconds":"5"}\n',
     b'{"cmd":"pause","seconds":86401}\n', b'{"cmd":"pause","seconds":true}\n'],
)  # fmt: skip
def test_parse_invalid_requests(line):
    with pytest.raises(server.BadRequest):
        server.parse_request(line)


async def test_socket_is_private_and_answers_one_line(short_tmp):
    path = short_tmp / "cpad.sock"
    seen = []

    async def handler(req):
        seen.append(req)
        return {"ok": True, "echo": req["cmd"]}

    srv = server.ControlServer(path, handler)
    await srv.start()
    try:
        assert stat.S_IMODE(os.stat(path).st_mode) == 0o600
        reply = await asyncio.to_thread(client.request, path, {"cmd": "status"})
        bad = await asyncio.to_thread(client.request, path, {"cmd": "explode"})
    finally:
        await srv.close()
    assert reply == {"ok": True, "echo": "status"} and seen == [{"cmd": "status"}]
    assert bad["ok"] is False and "error" in bad
    assert not path.exists()


def test_client_reports_a_missing_daemon(short_tmp):
    with pytest.raises(client.DaemonNotRunning):
        client.request(short_tmp / "absent.sock", {"cmd": "status"})


# --- LaunchAgent ----------------------------------------------------------------------------

ZSH_ENV = """\
Restored session: Mon Oct 19
PATH=/Users/u/.local/bin:/opt/homebrew/bin:/usr/bin
HOME=/Users/u
LANG=en_US.UTF-8
LC_CTYPE=UTF-8
SHELL=/bin/zsh
USER=u
ANTHROPIC_API_KEY=sk-should-never-be-copied
GITHUB_TOKEN=ghp_nope
weird line
"""


def test_env_whitelist_never_copies_secrets():
    env = launchd.parse_env(ZSH_ENV)
    assert env == {
        "PATH": "/Users/u/.local/bin:/opt/homebrew/bin:/usr/bin", "HOME": "/Users/u", "LANG": "en_US.UTF-8",
        "LC_CTYPE": "UTF-8", "SHELL": "/bin/zsh", "USER": "u",
    }  # fmt: skip


def test_env_whitelist_keeps_the_language_variables():
    env = launchd.parse_env("CPA_LANG=zh\nLC_ALL=zh_CN.UTF-8\nLC_MESSAGES=zh_CN\nLANG=en_US.UTF-8\nLANGUAGE=x\n")
    assert env == {"CPA_LANG": "zh", "LC_ALL": "zh_CN.UTF-8", "LC_MESSAGES": "zh_CN", "LANG": "en_US.UTF-8"}


def test_lang_env_is_what_decides_the_language_and_nothing_else():
    env = {"PATH": "/usr/bin", "LANG": "zh_CN.UTF-8", "LC_ALL": "", "LC_MESSAGES": "en_US", "LC_CTYPE": "UTF-8",
           "CPA_LANG": "zh", "SECRET": "x"}  # fmt: skip
    assert launchd.lang_env(env) == {"CPA_LANG": "zh", "LC_MESSAGES": "en_US", "LANG": "zh_CN.UTF-8"}
    assert launchd.lang_env({}) == {}


def test_capture_login_env_runs_zsh_lic_env():
    calls = []

    def runner(argv, **kw):
        calls.append(argv)
        return launchd.Completed(0, ZSH_ENV)

    assert launchd.capture_login_env(runner)["USER"] == "u"
    assert calls == [["/bin/zsh", "-lic", "env"]]


def test_plist_runs_cpad_daemon_at_load_and_keeps_it_alive(tmp_path):
    doc = launchd.plist_dict(["/venv/bin/python", "-m", "cpad", "daemon"], tmp_path / "home", cpa_home_env="/h")
    assert doc["Label"] == launchd.LABEL and doc["RunAtLoad"] is True and doc["KeepAlive"] is True
    assert doc["ProgramArguments"] == ["/venv/bin/python", "-m", "cpad", "daemon"]
    assert doc["EnvironmentVariables"] == {"CPA_HOME": "/h"}
    assert doc["StandardErrorPath"].startswith(str(tmp_path / "home"))


def test_plist_carries_the_login_language_so_cpad_writes_in_it(tmp_path):
    """Under launchd cpad has no LANG of its own: the login shell's language variables go into the plist."""
    lang = {"LANG": "zh_CN.UTF-8", "CPA_LANG": "zh"}
    doc = launchd.plist_dict(["/py"], tmp_path, cpa_home_env="/h", env=lang)
    assert doc["EnvironmentVariables"] == {"CPA_HOME": "/h", "LANG": "zh_CN.UTF-8", "CPA_LANG": "zh"}
    assert launchd.plist_dict(["/py"], tmp_path, cpa_home_env=None, env=lang)["EnvironmentVariables"] == lang
    assert "EnvironmentVariables" not in launchd.plist_dict(["/py"], tmp_path, cpa_home_env=None)


def test_install_writes_plist_and_bootstraps(tmp_path):
    fake = FakeLaunchd()
    agents = tmp_path / "LaunchAgents"
    done = launchd.install(["/py", "-m", "cpad", "daemon"], tmp_path / "home", agents, runner=fake, uid=501,
                           clock=fake.clock)  # fmt: skip
    path = done.path
    assert plistlib.loads(path.read_bytes())["Label"] == launchd.LABEL
    assert ["launchctl", "bootstrap", "gui/501", str(path)] in fake.calls
    launchd.uninstall(agents, runner=fake, uid=501)
    assert not path.exists() and fake.calls[-1] == ["launchctl", "bootout", f"gui/501/{launchd.LABEL}"]


# --- logging --------------------------------------------------------------------------------


def test_redact_text_and_argv():
    token = redact.redact_text("hello")
    assert token.startswith("<text len=5 sha8=") and "hello" not in token
    argv = redact.redact_argv(["claude", "--bg", "--name", "cpa/x", "--", "secret plan"])
    assert argv[:5] == ["claude", "--bg", "--name", "cpa/x", "--"] and "secret" not in argv[5]
    assert redact.redact_argv(["claude", "--version"]) == ["claude", "--version"]


def test_redact_stderr_truncates_and_masks():
    out = redact.redact_stderr("error: secret plan " + "x" * 500, ("secret plan",))
    assert "secret plan" not in out and len(out.encode()) <= 200 + 40


def test_log_records_never_contain_bodies(tmp_path):
    logger = setup.configure(tmp_path, content=False)
    logger.getChild("t").info("item", extra={"fields": {"text": "my private note", "id": "3QZK"}})
    for handler in logger.handlers:
        handler.flush()
    line = (tmp_path / "cpad.log").read_text().strip().splitlines()[-1]
    doc = json.loads(line)
    assert doc["id"] == "3QZK" and "my private note" not in line and doc["text"].startswith("<text len=15")
    setup.reset(logger)


def test_content_logging_is_opt_in_with_a_warning(tmp_path, capsys):
    logger = setup.configure(tmp_path, content=True)
    logger.getChild("t").info("item", extra={"fields": {"text": "visible"}})
    for handler in logger.handlers:
        handler.flush()
    assert "visible" in (tmp_path / "cpad.log").read_text()
    assert "CPA_LOG_CONTENT" in capsys.readouterr().err
    setup.reset(logger)


def test_redact_filter_on_plain_attributes():
    record = logging.LogRecord("x", logging.INFO, __file__, 1, "m", None, None)
    record.prompt = "do it"
    redact.RedactFilter().filter(record)
    assert record.prompt.startswith("<text len=5")
