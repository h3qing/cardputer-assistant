"""Agent requests end to end with fake-claude (05 §7.5, §9.2; US-07, US-09, US-10)."""

from __future__ import annotations

import asyncio
import os

import pytest

from cpad.cli.main import main as cpa
from tests.support.fake_claude import bg_session, tty_session
from tests.support.rig import until

HOSTILE = ["--dangerously-skip-permissions", "-c", "--permission-mode=bypassPermissions", "$(rm -rf ~)", "; ls",
           "nul\u0000byte", "a" * 501, "  -c test"]  # fmt: skip
PASSES_AS_DATA = {"$(rm -rf ~)", "; ls"}


async def test_spawn_argv_hostile_texts(rigs, monkeypatch):
    rig = rigs()
    await rig.start()
    await rig.synced()
    for n, text in enumerate(HOSTILE):
        result = await rig.request({"t": "agent.spawn", "id": f"d{100 + n}", "project": "cpa", "text": text})
        if text in PASSES_AS_DATA:
            assert result["ok"] is True, text
        else:
            assert (result["ok"], result["code"]) == (False, "E_BAD_FIELD"), text
    spawned = [c["argv"] for c in rig.claude.spawns()]
    assert [argv[argv.index("--") + 1 :] for argv in spawned] == [[t] for t in HOSTILE if t in PASSES_AS_DATA]
    for argv in spawned:
        assert argv[1:7] == ["--bg", "--name", argv[3], "--permission-mode", "plan", "--"]

    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    assert cpa(["queue", "add", "cpa", "--", "-c evil"]) == 0
    await until(lambda: any(i.qid == "q1" for i in rig.daemon.state.queue.items))
    result = await rig.request({"t": "agent.spawn", "id": "d200", "project": "cpa", "text": "harmless", "qid": "q1"})
    assert (result["ok"], result["code"]) == (False, "E_BAD_FIELD")
    assert len(rig.claude.spawns()) == len(PASSES_AS_DATA)


async def test_spawn_default_permission_mode_plan(rigs, monkeypatch):
    monkeypatch.setenv("CPA_TEST_SECRET", "must-not-leak")
    rig = rigs(projects=(("cpa", 1, None), ("blog", 2, "acceptEdits")))
    await rig.start()
    await rig.synced()
    result = await rig.request({"t": "agent.spawn", "id": "d7", "project": "cpa",
                                "text": "Write native tests for the chord detector"})  # fmt: skip
    assert result["ok"] is True and result["name"] == "cpa/write-native-tests-for" and len(result["short"]) == 8
    (call,) = rig.claude.spawns()
    assert call["argv"][1:] == ["--bg", "--name", "cpa/write-native-tests-for", "--permission-mode", "plan", "--",
                                "Write native tests for the chord detector"]  # fmt: skip
    assert os.path.realpath(call["cwd"]) == rig.roots["cpa"]
    assert call["env"]["FAKE_CLAUDE_DIR"] == str(rig.claude.dir) and "CPA_TEST_SECRET" not in call["env"]
    result = await rig.request({"t": "agent.spawn", "id": "d8", "project": "blog", "text": "draft a post"})
    assert result["ok"] is True and "acceptEdits" in rig.claude.spawns()[-1]["argv"]
    spawned = [e for e in rig.events() if e["ev"] == "agent.spawned"]
    assert len(spawned) == 2 and all("text" not in e for e in spawned)
    await until(lambda: any(s["short"] == result["short"] for s in rig.fake.snapshots["sessions"]["sessions"]))


async def test_spawn_crash_after_exit_zero_is_reported(rigs):
    rig = rigs()
    rig.claude.update(spawn_mode="crash")
    await rig.start()
    await rig.synced()
    result = await rig.request({"t": "agent.spawn", "id": "d7", "project": "cpa", "text": "will crash"})
    assert (result["ok"], result["code"]) == (False, "E_SPAWN")
    assert [e["ev"] for e in rig.events() if e["ev"].startswith("agent.")] == ["agent.spawn_failed"]


async def test_bypass_mode_rejected(rigs, monkeypatch):
    raw = {"version": 1, "projects": [{"key": "cpa", "name": "cpa", "slot": 1, "root": "/tmp",
                                       "spawn_permission_mode": "bypassPermissions"}]}  # fmt: skip
    rig = rigs(raw_projects=raw)
    await rig.start()
    await rig.synced()
    result = await rig.request({"t": "agent.spawn", "id": "d7", "project": "cpa", "text": "anything"})
    assert (result["ok"], result["code"]) == (False, "E_UNKNOWN_PROJECT")
    assert rig.claude.spawns() == []
    assert rig.daemon.status()["projects_error"]

    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    before = rig.paths.projects.read_bytes()
    assert cpa(["project", "add", "blog", rig.roots["cpa"], "--slot", "2", "--mode", "bypassPermissions"]) != 0
    assert rig.paths.projects.read_bytes() == before


async def test_inbox_debounce_and_dismiss(rigs):
    rig = rigs(config={"inbox_idle_s": 1.0})
    await rig.start()
    root = rig.roots["cpa"]
    rig.claude.update(sessions=[tty_session("8d3e7a19", "架构设计", root, "idle"),
                                bg_session("2a9c4f17", "cpa/等待权限确认", root, "waiting")])  # fmt: skip
    await rig.synced()

    def rows():
        return {s["sid"]: s for s in rig.fake.snapshots.get("sessions", {}).get("sessions", [])}

    await until(lambda: rows().get("2a9c4f17", {}).get("need") is True)
    assert rows()["8d3e7a19"]["need"] is False, "idle needs 1 s (inbox_idle_s) before it counts"
    await until(lambda: rows()["8d3e7a19"]["need"] is True, timeout=4)
    ack = await rig.request({"t": "inbox.dismiss", "id": "d20", "sid": "2a9c4f17"})
    assert ack["t"] == "ack"
    await until(lambda: rows()["2a9c4f17"]["need"] is False)
    unknown = await rig.request({"t": "inbox.dismiss", "id": "d21", "sid": "ffffffff"})
    assert unknown["code"] == "E_UNKNOWN_SESSION"
    rig.claude.update(sessions=[tty_session("8d3e7a19", "架构设计", root, "busy"),
                                bg_session("2a9c4f17", "cpa/等待权限确认", root, "busy")])  # fmt: skip
    await until(lambda: rows()["2a9c4f17"]["st"] == "busy")
    rig.claude.update(sessions=[bg_session("2a9c4f17", "cpa/等待权限确认", root, "waiting")])
    await until(lambda: rows().get("2a9c4f17", {}).get("need") is True, timeout=4)
    projects = rig.fake.snapshots["projects"]["projects"]
    assert projects[0]["need"] == 1 and rig.fake.snapshots["sessions"]["total"] == 1


async def test_reply_disabled_by_default(rigs):
    rig = rigs()
    rig.claude.update(sessions=[bg_session("2a9c4f17", "cpa/x", "/tmp", "idle")])
    await rig.start()
    await rig.synced()
    hello = rig.fake.host_messages("hello")[0]
    assert hello["caps"] == {"spawn": True, "reply": False, "recs": True, "todo": True}  # recs / todo: v0.4.1
    result = await rig.request({"t": "agent.reply", "id": "d8", "sid": "2a9c4f17", "text": "looks good"})
    assert (result["ok"], result["code"]) == (False, "E_NOT_REPLYABLE")
    assert not any(argv[:1] == ["stop"] or "--resume" in argv for argv in rig.claude.argvs())


@pytest.mark.parametrize("fork", [False, True])
async def test_reply_fork_detected(rigs, fork):
    rig = rigs(config={"experimental_bg_reply": True})
    await rig.start()
    session = bg_session("2a9c4f17", "cpa/write-tests", rig.roots["cpa"], "idle")
    rig.claude.update(sessions=[session], resume_mode="fork" if fork else "same")
    await rig.synced()
    assert rig.fake.host_messages("hello")[0]["caps"] == {"spawn": True, "reply": True, "recs": True, "todo": True}
    await until(lambda: any(s["sid"] == "2a9c4f17" for s in rig.fake.snapshots["sessions"]["sessions"]))
    result = await rig.request({"t": "agent.reply", "id": "d8", "sid": "2a9c4f17", "text": "add a test plan"})
    resume = next(argv for argv in rig.claude.argvs() if "--resume" in argv)
    assert resume == ["--bg", "--resume", session["sessionId"], "--", "add a test plan"]
    assert ["stop", "2a9c4f17"] in rig.claude.argvs()
    if not fork:
        assert result["ok"] is True
        return
    assert (result["ok"], result["code"]) == (False, "E_REPLY_FORKED")
    await asyncio.sleep(0.4)
    second = await rig.request({"t": "agent.reply", "id": "d9", "sid": "2a9c4f17", "text": "again"})
    assert second["code"] == "E_NOT_REPLYABLE", "reply stays off for the rest of this run"
    assert [e["ev"] for e in rig.events() if e["ev"].startswith("agent.")] == ["agent.reply_failed"]
