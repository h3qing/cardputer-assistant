"""Claude Code adapter (05 §7.5 as amended by docs/gates/cli-gates.md): parsing, argv, spawn, reply."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from cpad.agents.base import ReplyRequest, SpawnRequest
from cpad.agents.claude_code import adapter, argv, parse
from cpad.agents.claude_code.process import ProcResult

FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "agents"
LISTING = (FIXTURES / "agents-2.1.272.json").read_text(encoding="utf-8")
LISTING_ALL = (FIXTURES / "agents-all-2.1.272.json").read_text(encoding="utf-8")
HOSTILE = ("--dangerously-skip-permissions", "-c", "--permission-mode=bypassPermissions", "  -c test")

# --- parsing --------------------------------------------------------------------------------


def test_listing_is_parsed_leniently():
    sessions = {s.sid: s for s in parse.parse_sessions(LISTING)}
    assert set(sessions) == {"8d3e7a19", "5c2e8a14", "2a9c4f17", "a1b2c3d4", "b2c3d4e5", "c3d4e5f6"}
    assert sessions["8d3e7a19"].kind == "tty" and sessions["8d3e7a19"].short is None
    assert sessions["2a9c4f17"].kind == "bg" and sessions["2a9c4f17"].short == "2a9c4f17"
    assert sessions["b2c3d4e5"].kind == "bg" and sessions["b2c3d4e5"].status == "shell"
    assert sessions["a1b2c3d4"].status == "waiting" and sessions["a1b2c3d4"].state == "blocked"
    assert sessions["c3d4e5f6"].status == "unknown"
    assert sessions["5c2e8a14"].name == "Acme Web 登录页面改版"


def test_listing_all_keeps_states_without_status():
    sessions = {s.short: s for s in parse.parse_sessions(LISTING_ALL)}
    assert sessions["7a3f5c21"].state == "failed" and sessions["7a3f5c21"].status == "unknown"


@pytest.mark.parametrize("text", ["", "not json", '{"agents": 1}', "42"])
def test_unparseable_listing_raises(text):
    with pytest.raises(parse.AgentError):
        parse.parse_sessions(text)


def test_listing_wrapped_in_an_object_is_accepted():
    assert len(parse.parse_sessions(json.dumps({"agents": json.loads(LISTING_ALL)}))) == 3


@pytest.mark.parametrize(
    ("stdout", "short"),
    [
        ("backgrounded · 2a9c4f17 · cpa-gate/g7\n  claude agents   list sessions\n", "2a9c4f17"),
        ("backgrounded · 9b0d2e4c\n", "9b0d2e4c"),
        ("backgrounded · 7a3f5c21 · cpa-gate/g8-nodash (idle — send a prompt to start)\n", "7a3f5c21"),
        ("Starting background service…\n", None),
        ("  claude attach 2a9c4f17\nbackgrounded · 2a9c4f17\n", None),
        ("backgrounded · 2A9C4F17\n", None),
        ("", None),
    ],
)
def test_short_id_comes_from_the_first_stdout_line_only(stdout, short):
    assert parse.parse_short(stdout) == short


def test_version_parsing():
    assert parse.parse_version("2.1.272 (Claude Code)\n") == "2.1.272"
    assert parse.parse_version("weird") == "weird"[:32]


# --- argv -----------------------------------------------------------------------------------


def test_spawn_argv_puts_text_after_double_dash_and_mode_explicitly():
    got = argv.spawn_argv("/bin/claude", "cpa/write-tests", "plan", "$(rm -rf ~)")
    assert got == ["/bin/claude", "--bg", "--name", "cpa/write-tests", "--permission-mode", "plan", "--", "$(rm -rf ~)"]


@pytest.mark.parametrize("mode", ["bypassPermissions", "yolo", ""])
def test_spawn_argv_refuses_unknown_and_bypass_modes(mode):
    with pytest.raises(ValueError):
        argv.spawn_argv("/bin/claude", "n", mode, "text")


def test_resume_argv_has_no_other_flags():
    assert argv.resume_argv("/bin/claude", "2a9c4f17-d3b8", "next step") == [
        "/bin/claude", "--bg", "--resume", "2a9c4f17-d3b8", "--", "next step",
    ]  # fmt: skip


@pytest.mark.parametrize("text", [*HOSTILE, "", "a" * 501, "line\nbreak", "nul\x00", "　-x"])
def test_check_text_rejects_hostile_or_malformed_bodies(text):
    assert argv.check_text(text) == "E_BAD_FIELD"


@pytest.mark.parametrize("text", ["$(rm -rf ~)", "; ls", "fix the - bug", "中文任务", "a" * 500])
def test_check_text_accepts_ordinary_bodies(text):
    assert argv.check_text(text) is None


@pytest.mark.parametrize(
    ("key", "text", "name"),
    [
        ("cpa", "Write native tests for the chord detector", "cpa/write-native-tests-for"),
        ("cpa", "Fix: the LineFramer's \\r handling!", "cpa/fix-the-lineframers-r"),
        ("cpa", "中文 任务", "cpa/中文-任务"),
        ("waytolongkey", "alpha beta gamma delta", "waytolongkey/alpha-beta-gamma-de"),
        ("cpa", "Fix café menu", "cpa/fix-caf-menu"),
        ("cpa", "!!! ???", "cpa/task-7"),
    ],
)
def test_session_name_slug(key, text, name):
    got = argv.session_name(key, text, 7)
    assert got == name and len(got.encode()) <= 32


@pytest.mark.parametrize(
    ("key", "text", "name"),
    [
        ("cpa", "修复支付回调重试的问题，谢谢", "cpa/修复支付回调重试"),
        ("cardputer-as", "修复支付回调重试的问题", "cardputer-as/修复支付回调"),
        ("cpa", "把 README 里的安装步骤更新一下", "cpa/把-readme-里的安装"),  # 8 characters are 29 B: cut to 24
        ("cpa", "修复flaky测试", "cpa/修复flaky测试"),
        ("cpa", "／／ 中文", "cpa/中文"),
    ],
)
def test_a_chinese_task_is_named_by_its_first_eight_characters(key, text, name):
    """docs/20 U14: a dictated Chinese task is no longer task-N; an ASCII word counts as one character."""
    got = argv.session_name(key, text, 7)
    assert got == name and len(got.encode()) <= 32


# --- adapter with a fake runner -------------------------------------------------------------


class FakeRunner:
    def __init__(self, script):
        self.script = list(script)
        self.calls = []

    async def __call__(self, args, *, cwd=None, env=None, timeout):
        self.calls.append({"argv": list(args), "cwd": cwd, "env": env, "timeout": timeout})
        result = self.script.pop(0)
        return result(args) if callable(result) else result


def ok(stdout="", stderr=""):
    return ProcResult(0, stdout, stderr, False)


def _adapter(script, reply_enabled=False):
    runner = FakeRunner(script)
    env = {"PATH": "/usr/bin", "HOME": "/Users/user"}
    return adapter.ClaudeCodeAdapter("/opt/claude", env, runner=runner, reply_enabled=reply_enabled), runner


SPAWN = SpawnRequest(root="/Users/user/cpa", name="cpa/write-tests", text="write tests", permission_mode="plan")


async def test_probe_reports_version_and_caps():
    a, runner = _adapter([ok("2.1.272 (Claude Code)\n")], reply_enabled=True)
    caps = await a.probe()
    assert (caps.present, caps.version, caps.spawn, caps.reply) == (True, "2.1.272", True, True)
    assert runner.calls[0]["argv"] == ["/opt/claude", "--version"]


async def test_probe_missing_binary():
    a, _ = _adapter([ProcResult(None, "", "not found", False)])
    caps = await a.probe()
    assert not caps.present and not caps.spawn and not caps.reply


async def test_list_sessions_uses_spawn_env_and_all_flag():
    a, runner = _adapter([ok(LISTING_ALL)])
    sessions = await a.list_sessions(include_all=True)
    assert len(sessions) == 3
    assert runner.calls[0]["argv"] == ["/opt/claude", "agents", "--json", "--all"]
    assert runner.calls[0]["env"] == {"PATH": "/usr/bin", "HOME": "/Users/user"}


async def test_list_sessions_failure_raises():
    a, _ = _adapter([ProcResult(1, "", "boom", False)])
    with pytest.raises(parse.AgentError):
        await a.list_sessions()


async def test_spawn_parses_short_and_confirms_state():
    all_after = json.dumps([{"id": "2a9c4f17", "kind": "background", "sessionId": "2a9c4f17-d3b8", "state": "blocked"}])
    a, runner = _adapter([ok("[]"), ok("backgrounded · 2a9c4f17 · cpa/write-tests\n"), ok(all_after)])
    result = await a.spawn(SPAWN)
    assert (result.ok, result.short, result.name) == (True, "2a9c4f17", "cpa/write-tests")
    spawn_call = runner.calls[1]
    assert spawn_call["argv"][-2:] == ["--", "write tests"] and spawn_call["cwd"] == "/Users/user/cpa"
    assert spawn_call["timeout"] == 20


async def test_spawn_crash_after_exit_zero_is_a_failure():
    failed = json.dumps([{"id": "7a3f5c21", "kind": "background", "sessionId": "7a3f5c21-00", "state": "failed"}])
    a, _ = _adapter([ok("[]"), ok("backgrounded · 7a3f5c21 · cpa/write-tests\n"), ok(failed)])
    assert (await a.spawn(SPAWN)).code == "E_SPAWN"


async def test_spawn_without_short_falls_back_to_listing_diff():
    after = json.dumps([{"id": "1234abcd", "kind": "background", "sessionId": "1234abcd-ff", "name": "cpa/write-tests",
                         "status": "busy"}])  # fmt: skip
    a, _ = _adapter([ok("[]"), ok("Starting background service…\n"), ok(after), ok(after)])
    result = await a.spawn(SPAWN)
    assert (result.ok, result.short) == (True, "1234abcd")


async def test_spawn_errors():
    a, _ = _adapter([ok("[]"), ProcResult(None, "", "", True)])
    assert (await a.spawn(SPAWN)).code == "E_SPAWN"
    a, _ = _adapter([ok("[]"), ProcResult(2, "", "", False)])
    assert (await a.spawn(SPAWN)).code == "E_SPAWN"
    a, _ = _adapter([ok("[]"), ok("nothing\n"), ok("[]")])
    assert (await a.spawn(SPAWN)).code == "E_SPAWN"
    a, runner = _adapter([])
    assert (await a.spawn(SpawnRequest("/r", "n", "-c", "plan"))).code == "E_BAD_FIELD" and runner.calls == []
    assert (await a.spawn(SpawnRequest("/r", "n", "x", "bypassPermissions"))).code == "E_SPAWN"


def _bg(sid="2a9c4f17", status="idle", kind="background", session_id=None):
    return {"id": sid, "kind": kind, "sessionId": session_id or f"{sid}-d3b8-4e21-a6f5-7c0e9b1d2f38", "status": status,
            "state": "blocked", "name": "cpa/x"}  # fmt: skip


async def test_reply_disabled_by_default():
    a, runner = _adapter([])
    assert (await a.reply(ReplyRequest("2a9c4f17", "go on"))).code == "E_NOT_REPLYABLE" and runner.calls == []


async def test_reply_stop_then_resume_without_extra_flags():
    target = _bg()
    a, runner = _adapter(
        [ok(json.dumps([target])), ok(json.dumps([target])), ok("stopped 2a9c4f17\n"),
         ok("backgrounded · 2a9c4f17 · cpa/x\n"), ok(json.dumps([target]))],
        reply_enabled=True,
    )  # fmt: skip
    result = await a.reply(ReplyRequest("2a9c4f17", "looks good"))
    assert result.ok
    assert [c["argv"][1:] for c in runner.calls[2:4]] == [
        ["stop", "2a9c4f17"],
        ["--bg", "--resume", target["sessionId"], "--", "looks good"],
    ]


@pytest.mark.parametrize(
    "resume",
    [
        ok("backgrounded · 9b0d2e4c\n", "note: ... the flags you passed started a copy as 9b0d2e4c."),
        ok("backgrounded · 9b0d2e4c\n"),
    ],
)
async def test_reply_fork_is_detected_and_disables_reply(resume):
    target = _bg()
    a, _ = _adapter(
        [ok(json.dumps([target])), ok(json.dumps([target])), ok("stopped\n"), resume, ok(json.dumps([target]))],
        reply_enabled=True,
    )
    assert (await a.reply(ReplyRequest("2a9c4f17", "x"))).code == "E_REPLY_FORKED"
    assert not a.reply_available


async def test_reply_fork_detected_from_new_session_in_all_listing():
    target = _bg()
    copy = _bg("9b0d2e4c")
    a, _ = _adapter(
        [ok(json.dumps([target])), ok(json.dumps([target])), ok("stopped\n"), ok("backgrounded · 2a9c4f17\n"),
         ok(json.dumps([target, copy]))],
        reply_enabled=True,
    )  # fmt: skip
    assert (await a.reply(ReplyRequest("2a9c4f17", "x"))).code == "E_REPLY_FORKED"


@pytest.mark.parametrize(
    ("listing", "code"),
    [
        ([], "E_UNKNOWN_SESSION"),
        ([_bg(), _bg(session_id="2a9c4f17-ffff")], "E_UNKNOWN_SESSION"),
        ([_bg(status="busy")], "E_NOT_REPLYABLE"),
        ([_bg(kind="interactive")], "E_NOT_REPLYABLE"),
    ],
)
async def test_reply_requires_a_unique_idle_background_session(listing, code):
    a, runner = _adapter([ok(json.dumps(listing))], reply_enabled=True)
    assert (await a.reply(ReplyRequest("2a9c4f17", "x"))).code == code
    assert len(runner.calls) == 1


async def test_reply_refuses_dash_text_before_running_anything():
    a, runner = _adapter([], reply_enabled=True)
    assert (await a.reply(ReplyRequest("2a9c4f17", "--resume x"))).code == "E_BAD_FIELD" and runner.calls == []


async def test_reply_stop_failure():
    target = _bg()
    a, _ = _adapter([ok(json.dumps([target])), ok(json.dumps([target])), ProcResult(1, "", "", False)], True)
    assert (await a.reply(ReplyRequest("2a9c4f17", "x"))).code == "E_INTERNAL"


# --- the real subprocess runner -------------------------------------------------------------


async def test_process_runner_captures_output(tmp_path):
    from cpad.agents.claude_code.process import run

    result = await run(
        ["/bin/sh", "-c", "echo out; echo err >&2; exit 3"], cwd=str(tmp_path), env={"HOME": str(tmp_path)}, timeout=5
    )  # never HOME-less: the child would use the real home (#71)
    assert (result.rc, result.stdout, result.stderr, result.timed_out) == (3, "out\n", "err\n", False)


async def test_process_runner_timeout_and_missing_binary(tmp_path):
    from cpad.agents.claude_code.process import run

    slow = await run(["/bin/sleep", "5"], timeout=0.2)
    assert slow.timed_out and slow.rc is None
    missing = await run([str(tmp_path / "no-claude")], timeout=1)
    assert missing.rc is None and missing.stderr == "FileNotFoundError"
