"""Hub reduce (05 §7.3, §7.5, §7.6): device requests -> new state + ordered effects. No I/O here."""

from __future__ import annotations

from dataclasses import replace
from datetime import UTC

import pytest

from cpad.agents.base import ReplyResult, SessionInfo, SpawnResult
from cpad.events import records
from cpad.hub import effects as fx
from cpad.hub import inputs
from cpad.hub.reduce import failure_reply, reduce
from cpad.hub.state import AgentInfo, HubState
from cpad.inbox.need import Tracked
from cpad.notes.render import note_entry
from cpad.projects.registry import Project
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.queue.fold import fold_queue

AT = "2026-10-19T17:00:00.000+00:00"
CPA = Project("cpa", "Cardputer 助手", 1, "/r/cpa")
BROKEN = Project("old", "old", 2, "/gone", ok=False)
LOOSE = Project("loose", "家务", 3, None)  # a named group without a folder (docs/25 §8): nowhere to spawn
DEV = "020000000001"


def _state(**kw):
    base = HubState(projects=(CPA, BROKEN, LOOSE), agent=AgentInfo(present=True, version="2.1.272"), dev=DEV)
    return replace(base, **kw)


def _req(msg, duplicate=False):
    return inputs.DeviceRequest(msg=msg, now=100.0, wall=0.0, duplicate=duplicate)


def _note(project="cpa", item_id="3QZK1M7-0000C-0003", dest=None):
    return m.OutboxItem(item_id, "note", m.At(41000, None), m.NoteBody("idea", project, dest))


def _queue_item(intent="spawn", sid=None):
    return m.OutboxItem("3QZK1M7-0000C-0004", "queue", m.At(1), m.QueueBody("Write tests", "cpa", intent, sid, None))


def _kinds(effects):
    return [type(e).__name__ for e in effects]


# --- outbox.item ----------------------------------------------------------------------------


def test_new_note_appends_then_acks_then_renders():
    state, effects = reduce(_state(), _req(_note()))
    assert _kinds(effects) == ["Append", "Send", "WriteNote"]
    (record,) = effects[0].records
    assert record["ev"] == "item.received" and record["dev"] == DEV and "orphan" not in record
    assert effects[1].msg == m.OutboxAck(re="3QZK1M7-0000C-0003", status="stored")
    assert effects[2].record == record


def test_duplicate_is_acked_without_any_event():
    _, effects = reduce(_state(), _req(_note(), duplicate=True))
    assert effects == (fx.Send(m.OutboxAck(re="3QZK1M7-0000C-0003", status="duplicate")),)


def test_note_for_a_removed_project_is_kept_as_orphan():
    _, effects = reduce(_state(), _req(_note(project="deleted")))
    record = effects[0].records[0]
    assert record["orphan"] is True and record["body"]["project"] == "deleted"


def test_a_todo_note_is_stored_with_its_dest_and_rendered():
    _, effects = reduce(_state(), _req(_note(dest="todo")))
    assert _kinds(effects) == ["Append", "Send", "WriteNote"]
    (record,) = effects[0].records
    assert record["body"]["dest"] == "todo" and effects[2].record == record
    assert effects[1].msg == m.OutboxAck(re="3QZK1M7-0000C-0003", status="stored")
    assert note_entry(effects[2].record, UTC)[1] == "- [ ] ~00:00 #cpa idea ^cpa-3qzk1m7-0000c-0003"


def test_a_note_without_dest_records_exactly_the_v03_body_and_line():
    _, effects = reduce(_state(), _req(_note()))
    assert effects[0].records[0]["body"] == {"text": "idea", "project": "cpa"}
    assert note_entry(effects[2].record, UTC)[1] == "- ~00:00 #cpa idea ^cpa-3qzk1m7-0000c-0003"


@pytest.mark.parametrize("dest", ["clip", "ww.plugin.todoist", "file.sb-inbox"])
def test_an_unknown_dest_is_stored_as_a_plain_note_and_logged_without_text(dest):
    _, effects = reduce(_state(), _req(_note(dest=dest)))
    assert _kinds(effects) == ["Append", "Send", "WriteNote"]
    received, downgraded = effects[0].records
    assert received["ev"] == "item.received" and effects[2].record == received
    assert downgraded == {"ev": "dest.downgraded", "at": received["at"], "item": "3QZK1M7-0000C-0003", "dest": dest}
    assert "idea" not in str(downgraded)
    assert note_entry(effects[2].record, UTC)[1] == "- ~00:00 #cpa idea ^cpa-3qzk1m7-0000c-0003", "the plain daily line"


@pytest.mark.parametrize("dest", ["daily", "todo"])
def test_a_known_dest_is_never_downgraded(dest):
    _, effects = reduce(_state(), _req(_note(dest=dest)))
    assert [r["ev"] for r in effects[0].records] == ["item.received"]


def test_a_duplicate_with_an_unknown_dest_logs_nothing():
    _, effects = reduce(_state(), _req(_note(dest="clip"), duplicate=True))
    assert effects == (fx.Send(m.OutboxAck(re="3QZK1M7-0000C-0003", status="duplicate")),)


@pytest.mark.parametrize("intent", ["todo", "spawn", "feedback"])
def test_queued_intents_are_recorded_never_executed(intent):
    sid = "5c2e8a14" if intent == "feedback" else None
    _, effects = reduce(_state(), _req(_queue_item(intent, sid)))
    assert _kinds(effects) == ["Append", "Send"]
    item_rec, queue_rec = effects[0].records
    assert queue_rec["ev"] == "queue.added" and queue_rec["intent"] == intent and queue_rec["qid"] is None
    assert not any(isinstance(e, fx.RunSpawn | fx.RunReply) for e in effects)


# --- agent.spawn ----------------------------------------------------------------------------


def test_spawn_starts_with_plan_mode_and_a_slug_name():
    state, effects = reduce(_state(), _req(m.AgentSpawn("d7", "cpa", "Write native tests for the chord detector")))
    (run,) = effects
    assert isinstance(run, fx.RunSpawn) and run.re == "d7"
    assert (run.request.root, run.request.permission_mode) == ("/r/cpa", "plan")
    assert run.request.name == "cpa/write-native-tests-for"
    assert state.agent_busy == "d7"


@pytest.mark.parametrize(
    ("change", "msg", "code"),
    [
        ({"agent_busy": "d1"}, m.AgentSpawn("d7", "cpa", "x"), "E_BUSY"),
        ({}, m.AgentSpawn("d7", "nope", "x"), "E_UNKNOWN_PROJECT"),
        ({}, m.AgentSpawn("d7", "old", "x"), "E_UNKNOWN_PROJECT"),
        ({}, m.AgentSpawn("d7", "loose", "x"), "E_UNKNOWN_PROJECT"),
        ({"agent": AgentInfo()}, m.AgentSpawn("d7", "cpa", "x"), "E_NO_AGENT"),
        ({}, m.AgentSpawn("d7", "cpa", "x", qid="q9"), "E_UNKNOWN_ITEM"),
    ],
)
def test_spawn_refusals_answer_immediately(change, msg, code):
    state, effects = reduce(_state(**change), _req(msg))
    assert effects == (fx.Send(m.AgentResult(re="d7", ok=False, code=code)),)
    assert state.agent_busy == change.get("agent_busy")


def test_spawn_from_queue_uses_the_full_saved_text():
    long_text = "Refactor the outbox " + "x" * 300
    queue = fold_queue([records.queue_added(qid="q3", project="cpa", text=long_text, intent="spawn", origin="device",
                                            at=AT)])  # fmt: skip
    _, effects = reduce(_state(queue=queue), _req(m.AgentSpawn("d7", "cpa", "Refactor the outbox", qid="q3")))
    assert effects[0].request.text == long_text and effects[0].qid == "q3"


def test_an_item_handed_from_a_recording_always_starts_in_plan_mode():
    """docs/23 §3.4: origin inbox -> plan whatever the project's mode; other items keep the project's mode."""
    queue = fold_queue([records.queue_handed(qid="q3", project="cpa", text="把设置页改成英文", intent="spawn",
                                             rec="3QZK1M7-0000A-0001", at=AT),
                        records.queue_added(qid="q4", project="cpa", text="typed on the device", intent="spawn",
                                            origin="device", at=AT)])  # fmt: skip
    state = _state(projects=(replace(CPA, mode="acceptEdits"), BROKEN), queue=queue)
    modes = [reduce(state, _req(m.AgentSpawn("d7", "cpa", "x", qid=qid)))[1][0].request.permission_mode
             for qid in ("q3", "q4", None)]  # fmt: skip
    assert modes == ["plan", "acceptEdits", "acceptEdits"]


def test_spawn_from_queue_still_refuses_dash_text():
    queue = fold_queue([records.queue_added(qid="q3", project="cpa", text="--dangerously-skip-permissions",
                                            intent="spawn", origin="cli", at=AT)])  # fmt: skip
    _, effects = reduce(_state(queue=queue), _req(m.AgentSpawn("d7", "cpa", "harmless", qid="q3")))
    assert effects == (fx.Send(m.AgentResult(re="d7", ok=False, code="E_BAD_FIELD")),)


def test_spawn_result_is_logged_before_it_is_sent():
    state = _state(agent_busy="d7")
    finished = inputs.AgentFinished("d7", "spawn", SpawnResult(True, "cpa/x", "2a9c4f17"), project="cpa", qid="q3",
                                    sid=None, now=1.0, wall=0.0)  # fmt: skip
    state, effects = reduce(state, finished)
    assert _kinds(effects) == ["Append", "Send"] and state.agent_busy is None
    assert [r["ev"] for r in effects[0].records] == ["agent.spawned", "queue.spawned"]
    assert effects[1].msg == m.AgentResult(re="d7", ok=True, name="cpa/x", short="2a9c4f17")


def test_spawn_failure_is_reported_with_its_code():
    finished = inputs.AgentFinished("d7", "spawn", SpawnResult(False, "cpa/x", code="E_SPAWN"), project="cpa",
                                    qid=None, sid=None, now=1.0, wall=0.0)  # fmt: skip
    _, effects = reduce(_state(agent_busy="d7"), finished)
    assert effects[0].records[0]["ev"] == "agent.spawn_failed"
    assert effects[1].msg == m.AgentResult(re="d7", ok=False, code="E_SPAWN")


# --- agent.reply ----------------------------------------------------------------------------


def _tracked(kind="bg", status="idle", sid="2a9c4f17"):
    info = SessionInfo(f"{sid}-e6eb", sid, sid if kind == "bg" else None, kind, status, None, "cpa/x", "/r/cpa")
    return Tracked(info, "cpa", 0.0)


def test_reply_disabled_by_default():
    _, effects = reduce(_state(tracked=(_tracked(),)), _req(m.AgentReply("d8", "2a9c4f17", "go")))
    assert effects == (fx.Send(m.AgentResult(re="d8", ok=False, code="E_NOT_REPLYABLE")),)


@pytest.mark.parametrize(
    ("tracked", "code"),
    [
        ((), "E_UNKNOWN_SESSION"),
        ((_tracked(kind="tty"),), "E_NOT_REPLYABLE"),
        ((_tracked(status="busy"),), "E_NOT_REPLYABLE"),
    ],
)
def test_reply_targets_only_idle_background_sessions(tracked, code):
    agent = AgentInfo(present=True, reply=True)
    _, effects = reduce(_state(agent=agent, tracked=tracked), _req(m.AgentReply("d8", "2a9c4f17", "go")))
    assert effects == (fx.Send(m.AgentResult(re="d8", ok=False, code=code)),)


def test_reply_runs_and_a_fork_disables_reply():
    state = _state(agent=AgentInfo(present=True, reply=True), tracked=(_tracked(),))
    state, effects = reduce(state, _req(m.AgentReply("d8", "2a9c4f17", "go")))
    assert isinstance(effects[0], fx.RunReply) and effects[0].request.sid == "2a9c4f17"
    finished = inputs.AgentFinished("d8", "reply", ReplyResult(False, "E_REPLY_FORKED"), project=None, qid=None,
                                    sid="2a9c4f17", now=1.0, wall=0.0)  # fmt: skip
    state, effects = reduce(state, finished)
    assert not state.agent.caps.reply and state.agent_busy is None
    assert effects[0].records[0]["ev"] == "agent.reply_failed"
    assert effects[1].msg.code == "E_REPLY_FORKED"


# --- queue.done / inbox.dismiss --------------------------------------------------------------


def test_queue_done():
    queue = fold_queue([records.queue_added(qid="q1", project="cpa", text="t", intent="todo", origin="cli", at=AT)])
    _, effects = reduce(_state(queue=queue), _req(m.QueueDone("d9", "q1")))
    assert _kinds(effects) == ["Append", "Send"] and effects[1].msg == m.Ack(re="d9")
    _, effects = reduce(_state(queue=queue), _req(m.QueueDone("d9", "q2")))
    assert effects == (fx.Send(m.ErrorMsg(re="d9", code="E_UNKNOWN_ITEM")),)


def test_inbox_dismiss_clears_need_until_state_changes():
    state = _state(tracked=(_tracked(status="waiting"),))
    state, _ = reduce(state, inputs.Polled(((state.tracked[0].info, "cpa"),), now=1.0))
    state, effects = reduce(state, _req(m.InboxDismiss("d10", "2a9c4f17")))
    assert effects[0] == fx.Send(m.Ack(re="d10"))
    pushed = {type(e.msg).__name__: e.msg for e in effects if isinstance(e, fx.Push)}
    assert pushed["SessionsSnapshot"].sessions[0].need is False
    _, effects = reduce(state, _req(m.InboxDismiss("d11", "ffffffff")))
    assert effects == (fx.Send(m.ErrorMsg(re="d11", code="E_UNKNOWN_SESSION")),)


# --- link, polling, appended -----------------------------------------------------------------


def test_link_ready_pushes_all_three_snapshots():
    state, effects = reduce(_state(dev=None), inputs.LinkReady(dev=DEV, now=1.0))
    assert state.dev == DEV
    assert [type(e.msg).__name__ for e in effects] == ["ProjectsSnapshot", "SessionsSnapshot", "QueueSnapshot"]
    assert all(e.msg.rev >= 1 for e in effects)


def test_polling_pushes_only_changes():
    info = _tracked(status="busy").info
    state, effects = reduce(_state(), inputs.Polled(((info, "cpa"),), now=1.0))
    assert {type(e.msg).__name__ for e in effects} == {"ProjectsSnapshot", "SessionsSnapshot", "QueueSnapshot"}
    state, effects = reduce(state, inputs.Polled(((info, "cpa"),), now=4.0))
    assert effects == ()


def test_appended_records_refold_the_queue():
    rec = records.queue_added(qid="q1", project="cpa", text="t", intent="todo", origin="cli", at=AT)
    state, effects = reduce(_state(), inputs.Appended((rec,), now=1.0))
    assert [i.qid for i in state.queue.items] == ["q1"]
    assert any(isinstance(e.msg, m.QueueSnapshot) for e in effects)


def test_projects_loaded_and_probe():
    state, _ = reduce(_state(), inputs.ProjectsLoaded((CPA,), now=1.0))
    assert state.projects == (CPA,)
    state, effects = reduce(state, inputs.AgentProbed(present=False, version="", errored=True))
    assert state.agent.status == "error" and effects == ()


def test_link_down_keeps_state():
    state = _state(agent_busy="d7")
    assert reduce(state, inputs.LinkDown())[0] == state


# --- failure replies for invalid device requests ----------------------------------------------


@pytest.mark.parametrize(
    ("line", "expected"),
    [
        (b'{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0003","kind":"spawn","at":{"up_ms":1},"body":{}}\n',
         m.OutboxAck(re="3QZK1M7-0000C-0003", status="rejected", code="E_BAD_FIELD")),
        (b'{"v":0,"t":"outbox.item","id":"d5","kind":"note","at":{"up_ms":1},"body":{"text":"x"}}\n',
         m.ErrorMsg(re="d5", code="E_BAD_FIELD", field="id")),
        (b'{"v":0,"t":"agent.spawn","id":"d7","project":"cpa","text":"-c"}\n',
         m.AgentResult(re="d7", ok=False, code="E_BAD_FIELD")),
        (b'{"v":0,"t":"queue.done","id":"d9","qid":"x"}\n', m.ErrorMsg(re="d9", code="E_BAD_FIELD", field="qid")),
        (b'{"v":0,"t":"foo.bar","id":"d1"}\n', m.ErrorMsg(re="d1", code="E_UNKNOWN_TYPE")),
        (b'{"v":1,"t":"ping","id":"d2"}\n', m.ErrorMsg(re="d2", code="E_VERSION")),
        (b'{"v":0,"t":"ping","id":"d3"}\n', m.ErrorMsg(re="d3", code="E_UNKNOWN_TYPE")),
    ],
)  # fmt: skip
def test_failure_replies(line, expected):
    assert failure_reply(codec.decode_line(line)) == expected


def test_reconcile_repushes_only_what_the_device_lacks():
    state, _ = reduce(_state(), inputs.LinkReady(dev=DEV, now=1.0))
    revs = state.published.revs
    behind = m.Revs(revs.projects, revs.sessions - 1, revs.queue)
    _, effects = reduce(state, inputs.Reconcile(device_revs=behind, now=2.0))
    assert [type(e.msg).__name__ for e in effects] == ["SessionsSnapshot"]
    assert effects[0].msg.rev == revs.sessions
    assert reduce(state, inputs.Reconcile(device_revs=revs, now=2.0))[1] == ()
