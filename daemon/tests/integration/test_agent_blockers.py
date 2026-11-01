"""#25 (why a spawn failed, and the one-time fix) and #5 (needs-you during a permission wait or a question),
end to end with fake-claude in a temporary HOME."""

from __future__ import annotations

import asyncio
import shlex

from cpad.cli.main import main as cpa
from tests.support.fake_claude import bg_session, tty_session
from tests.support.rig import until


def _spawn(req_id: str) -> dict:
    return {"t": "agent.spawn", "id": req_id, "project": "cpa", "text": "write tests"}


async def _status_text(rig, monkeypatch, capsys) -> str:
    monkeypatch.setenv("CPA_HOME", str(rig.paths.home))
    capsys.readouterr()
    assert await asyncio.to_thread(cpa, ["status"]) == 0
    return capsys.readouterr().out


async def test_untrusted_folder_reason_and_fix(rigs, monkeypatch, capsys):
    rig = rigs()
    rig.claude.update(spawn_mode="untrusted")
    await rig.start()
    await rig.synced()
    result = await rig.request(_spawn("d7"))
    assert (result["ok"], result["code"], result["why"]) == (False, "E_SPAWN", "untrusted")
    (failed,) = [e for e in rig.events() if e["ev"] == "agent.spawn_failed"]
    assert failed["why"] == "untrusted" and "text" not in failed
    fix = f"cd {shlex.quote(rig.roots['cpa'])} && claude"
    assert fix in await _status_text(rig, monkeypatch, capsys)

    rig.claude.update(spawn_mode="ok")
    result = await rig.request(_spawn("d8"))
    assert result["ok"] is True and "why" not in result
    assert not any(fix in w for w in rig.daemon.status()["warnings"]), "a spawn that works clears the warning"


async def test_logged_out_cli_is_shown_and_spawn_refused(rigs, monkeypatch, capsys):
    rig = rigs()
    rig.claude.update(logged_in=False)
    await rig.start()
    await rig.synced()
    assert rig.daemon.status()["agent"]["login"] is False
    out = await _status_text(rig, monkeypatch, capsys)
    assert "login=no" in out and "claude auth login" in out

    result = await rig.request(_spawn("d7"))
    assert (result["ok"], result["code"], result["why"]) == (False, "E_SPAWN", "login")
    assert rig.claude.spawns() == [], "no session is started while logged out"
    assert [e.get("why") for e in rig.events() if e["ev"] == "agent.spawn_failed"] == ["login"]
    auth = [c for c in rig.claude.calls() if c["argv"][1:] == ["auth", "status"]]
    assert auth and all(c["env"]["HOME"] == str(rig.claude.dir) for c in auth), "checked in cpad's environment"

    rig.claude.update(logged_in=True)
    result = await rig.request(_spawn("d8"))
    assert result["ok"] is True and rig.daemon.status()["agent"]["login"] is True
    assert not any("claude auth login" in w for w in rig.daemon.status()["warnings"])


async def test_permission_wait_and_question_show_as_needs_you(rigs):
    rig = rigs()
    await rig.start()
    root = rig.roots["cpa"]
    rig.claude.update(sessions=[
        {**tty_session("8d3e7a19", "架构设计", root, "waiting"), "waitingFor": "permission prompt"},
        {**bg_session("a1b2c3d4", "cpa/plan", root, "waiting"), "waitingFor": "permission prompt"},
        {**bg_session("b2c3d4e5", "cpa/ask", root, "waiting"), "waitingFor": "input needed"},
        bg_session("2a9c4f17", "cpa/login", root, "idle", "blocked"),
        bg_session("c3d4e5f6", "cpa/done", root, "idle", "done"),
        bg_session("d4e5f6a7", "cpa/tests", root, "busy", "working"),
    ])  # fmt: skip
    await rig.synced()

    def rows() -> dict:
        return {s["sid"]: s for s in rig.fake.snapshots.get("sessions", {}).get("sessions", [])}

    await until(lambda: len(rows()) == 6)
    assert {sid for sid, row in rows().items() if row["need"]} == {"8d3e7a19", "a1b2c3d4", "b2c3d4e5", "2a9c4f17"}
    (project,) = rig.fake.snapshots["projects"]["projects"]
    assert (project["need"], project["run"]) == (4, 1)

    rig.claude.update(sessions=[bg_session("a1b2c3d4", "cpa/plan", root, "busy", "working")])
    await until(lambda: rows().get("a1b2c3d4", {}).get("need") is False and len(rows()) == 1)
