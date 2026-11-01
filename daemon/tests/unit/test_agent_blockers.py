"""Why a spawn fails (#25) and what a waiting session looks like (#5, gate G4), Claude Code 2.1.306 shapes."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from cpad.agents.base import SessionInfo, SpawnRequest, SpawnResult
from cpad.agents.claude_code import adapter, blockers, parse
from cpad.agents.claude_code.process import ProcResult
from cpad.events import records
from cpad.hub import inputs
from cpad.hub.reduce import reduce
from cpad.hub.state import AgentInfo, HubState
from cpad.inbox import need
from cpad.projects.registry import Project
from cpad.protocol import messages as m

FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "agents"
LISTING = (FIXTURES / "agents-2.1.306.json").read_text(encoding="utf-8")
ROOT = "/Users/user/Repos/Cardputer Assistant"
UNTRUSTED = f"Workspace not trusted. Run `claude` in {ROOT} once and accept the trust prompt, then retry.\n"
HOME_UNTRUSTED = (
    "Workspace not trusted. The home directory is trusted one session at a time — start this from an "
    "interactive terminal there, or from a project directory.\n"
)
LOGGED_OUT = '{"loggedIn":false,"authMethod":"none","apiProvider":"firstParty"}\n'
LOGGED_IN = '{"loggedIn":true,"authMethod":"claude.ai","apiProvider":"firstParty","email":"x@y.z","orgName":"o"}\n'
SPAWN = SpawnRequest(root=ROOT, name="cpa/write-tests", text="write tests", permission_mode="plan")


# --- stderr and auth status --------------------------------------------------------------------


@pytest.mark.parametrize(
    ("stderr", "why"),
    [
        (UNTRUSTED, "untrusted"),
        ("Starting background service…\n" + HOME_UNTRUSTED, "untrusted"),
        ("Workspace not trusted. /gone could not be resolved on disk.\n", "untrusted"),
        ("Not logged in · Please run /login\n", "login"),
        ("Login expired · Please run /login\n", "login"),
        ("Not logged in. Run claude auth login to authenticate.\n", "login"),
        ("error: spawn failed\n", None),
        ("", None),
        ("note: Workspace not trusted. is quoted here, not a refusal\n", None),
    ],
)
def test_spawn_stderr_is_classified(stderr, why):
    assert blockers.spawn_why(stderr) == why


def test_spawn_stderr_is_scanned_only_up_to_a_bound():
    assert blockers.spawn_why("x" * 10_000 + "\n" + UNTRUSTED) is None


@pytest.mark.parametrize(
    ("stdout", "logged_in"),
    [(LOGGED_OUT, False), (LOGGED_IN, True), ("Not logged in.\n", None), ('{"loggedIn": "no"}', None),
     ("[]", None), ("", None), ('{"loggedIn": true, "pad": "' + "x" * 70_000 + '"}', None)],
)  # fmt: skip
def test_auth_status_is_parsed_leniently(stdout, logged_in):
    assert blockers.parse_logged_in(stdout) is logged_in


def test_untrusted_root_is_remembered_until_a_spawn_there_works():
    state = blockers.after_spawn(blockers.Blockers(), ROOT, ok=False, why="untrusted")
    assert state.untrusted == frozenset({ROOT})
    assert blockers.after_spawn(state, "/elsewhere", ok=True, why=None).untrusted == frozenset({ROOT})
    assert blockers.after_spawn(state, ROOT, ok=True, why=None).untrusted == frozenset()
    assert blockers.after_spawn(state, ROOT, ok=False, why=None) == state


def test_login_failure_marks_logged_out():
    state = blockers.after_spawn(blockers.Blockers(logged_in=True), ROOT, ok=False, why="login")
    assert state.logged_in is False


def test_warnings_carry_the_exact_one_time_fix():
    projects = (Project("cpa", "cpa", 1, ROOT), Project("blog", "blog", 2, "/Users/user/docs"))
    state = blockers.Blockers(untrusted=frozenset({ROOT}), logged_in=False)
    login, trust = blockers.warnings(state, projects)
    assert "claude auth login" in login
    assert trust.startswith("project cpa: ") and "cd '/Users/user/Repos/Cardputer Assistant' && claude" in trust
    assert "trust prompt" in trust
    assert blockers.warnings(blockers.Blockers(logged_in=True), projects) == ()


# --- adapter ----------------------------------------------------------------------------------


class Runner:
    def __init__(self, script):
        self.script = list(script)
        self.calls = []

    async def __call__(self, args, *, cwd=None, env=None, timeout):
        self.calls.append(list(args[1:]))
        return self.script.pop(0)


def ok(stdout="", stderr="", rc=0):
    return ProcResult(rc, stdout, stderr, False)


def _adapter(script):
    runner = Runner(script)
    return adapter.ClaudeCodeAdapter("/opt/claude", {"HOME": "/h"}, runner=runner), runner


async def test_untrusted_spawn_reports_its_reason():
    a, _ = _adapter([ok("[]"), ok(stderr=UNTRUSTED, rc=1)])
    assert await a.spawn(SPAWN) == SpawnResult(False, code="E_SPAWN", why="untrusted")
    assert a.warnings((Project("cpa", "cpa", 1, ROOT),))[0].startswith("project cpa: ")


async def test_unexplained_spawn_failure_has_no_reason():
    a, _ = _adapter([ok("[]"), ok(stderr="boom\n", rc=1)])
    assert await a.spawn(SPAWN) == SpawnResult(False, code="E_SPAWN")
    assert a.warnings((Project("cpa", "cpa", 1, ROOT),)) == ()


async def test_check_login_runs_auth_status_read_only():
    a, runner = _adapter([ok(LOGGED_OUT, rc=1), ok(stdout="", rc=None)])
    assert await a.check_login() is False and a.logged_in is False
    assert runner.calls == [["auth", "status"]]
    assert await a.check_login() is None and a.logged_in is None


async def test_spawn_while_logged_out_is_refused_after_a_fresh_check():
    a, runner = _adapter([ok(LOGGED_OUT, rc=1), ok(LOGGED_OUT, rc=1)])
    await a.check_login()
    assert await a.spawn(SPAWN) == SpawnResult(False, code="E_SPAWN", why="login")
    assert runner.calls == [["auth", "status"], ["auth", "status"]], "no session is started while logged out"


async def test_spawn_after_logging_in_goes_ahead():
    a, runner = _adapter([ok(LOGGED_OUT, rc=1), ok(LOGGED_IN), ok("[]"), ok("backgrounded · 2a9c4f17 · cpa/x\n"),
                          ok("[]")])  # fmt: skip
    await a.check_login()
    result = await a.spawn(SPAWN)
    assert result.ok and result.short == "2a9c4f17" and a.logged_in is True
    assert runner.calls[1] == ["auth", "status"] and runner.calls[3][0] == "--bg"


# --- agents --json during a permission wait or a question (G4) ---------------------------------


def test_waiting_for_is_mapped_to_a_reason():
    sessions = {s.sid: s for s in parse.parse_sessions(LISTING)}
    assert sessions["8d3e7a19"].status == "waiting" and sessions["8d3e7a19"].waiting_for == "permission"
    assert sessions["5c2e8a14"].waiting_for == "question"
    assert sessions["0a1b2c3d"].waiting_for == "dialog"
    assert sessions["a1b2c3d4"].waiting_for == "permission" and sessions["a1b2c3d4"].state == "blocked"
    assert sessions["b2c3d4e5"].waiting_for == "question"
    assert sessions["e5f6a7b8"].waiting_for == "permission"
    assert sessions["f6a7b8c9"].waiting_for == "dialog", "an unknown reason still needs you"
    assert sessions["1a2b3c4d"].waiting_for is None and sessions["c3d4e5f6"].waiting_for is None


@pytest.mark.parametrize("value", [["permission prompt"], {"x": 1}, 7, None])
def test_a_malformed_waiting_for_is_still_a_wait(value):
    item = {"kind": "interactive", "sessionId": "8d3e7a19-4c2b-4a6e-9f10-2b7c5d8e3a61", "status": "waiting",
            "waitingFor": value}  # fmt: skip
    (session,) = parse.parse_sessions(json.dumps([item]))
    assert session.waiting_for == "dialog"


def _tracked(sid: str, project: str | None = "cpa") -> need.Tracked:
    info = {s.sid: s for s in parse.parse_sessions(LISTING)}[sid]
    (t,) = need.track((), ((info, project),), now=0.0)
    return t


@pytest.mark.parametrize(
    ("sid", "needed"),
    [
        ("8d3e7a19", True),  # interactive, permission prompt
        ("5c2e8a14", True),  # interactive, AskUserQuestion
        ("0a1b2c3d", True),  # interactive, dialog open
        ("a1b2c3d4", True),  # background, plan approval
        ("b2c3d4e5", True),  # background, question
        ("2a9c4f17", True),  # background idle + blocked: the turn failed (logged out)
        ("c3d4e5f6", False),  # background idle + done: needs you only after inbox_idle_s
        ("d4e5f6a7", False),  # background busy + working
        ("1a2b3c4d", False),  # interactive busy
    ],
)
def test_needs_you_right_away(sid, needed):
    assert need.needs(_tracked(sid), now=1.0, idle_s=60, dismissed=frozenset()) is needed


def test_blocked_needs_a_registered_project_and_respects_dismiss():
    assert not need.needs(_tracked("2a9c4f17", project=None), now=1.0, idle_s=60, dismissed=frozenset())
    t = _tracked("2a9c4f17")
    assert not need.needs(t, now=1.0, idle_s=60, dismissed=need.dismiss(frozenset(), t))


def test_blocked_but_running_is_not_waiting():
    info = SessionInfo("2a9c4f17-d3b8", "2a9c4f17", "2a9c4f17", "bg", "busy", "blocked", "cpa/x", ROOT)
    (t,) = need.track((), ((info, "cpa"),), now=0.0)
    assert not need.needs(t, now=999.0, idle_s=60, dismissed=frozenset())


# --- hub: the reason reaches the device and events.jsonl ---------------------------------------


@pytest.mark.parametrize("why", ["untrusted", "login"])
def test_spawn_failure_reason_is_logged_and_sent(why):
    state = HubState(projects=(Project("cpa", "cpa", 1, ROOT),), agent=AgentInfo(present=True), agent_busy="d7")
    finished = inputs.AgentFinished("d7", "spawn", SpawnResult(False, code="E_SPAWN", why=why), project="cpa",
                                    qid=None, sid=None, now=1.0, wall=0.0)  # fmt: skip
    _, effects = reduce(state, finished)
    (record,) = effects[0].records
    assert (record["ev"], record["code"], record["why"]) == ("agent.spawn_failed", "E_SPAWN", why)
    assert effects[1].msg == m.AgentResult(re="d7", ok=False, code="E_SPAWN", why=why)
    assert m.to_wire(effects[1].msg)["why"] == why


def test_records_without_a_reason_are_unchanged():
    rec = records.agent_spawn_failed("cpa", None, "E_SPAWN", "2026-10-21T00:00:00.000+00:00")
    assert "why" not in rec and "why" not in m.to_wire(m.AgentResult("d7", False, "E_SPAWN"))
    assert json.dumps(rec)
