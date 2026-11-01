"""EffectRunner (05 §7.1, §7.3): order of effects, no ack when the append fails, agent crashes contained."""

from __future__ import annotations

import asyncio

from cpad.agents.base import ReplyRequest, SpawnRequest
from cpad.events import records
from cpad.hub import effects as fx
from cpad.hub import inputs
from cpad.hub.runner import EffectRunner
from cpad.hub.state import HubState
from cpad.protocol import messages as m

AT = "2026-10-19T17:00:00.000+00:00"


class Link:
    ready = True

    def __init__(self):
        self.sent = []

    def reply(self, msg):
        self.sent.append(("reply", msg))

    def request(self, msg):
        self.sent.append(("request", msg))


class Events:
    def __init__(self, fail=False, others=()):
        self.fail, self.others = fail, others
        self.appended = []

    def append(self, recs, prepare=None):
        if self.fail:
            raise OSError(28, "No space left on device")
        written = tuple(prepare(()) if prepare else recs)
        self.appended += written
        return self.others, written


class Notes:
    def __init__(self):
        self.written = []

    def write(self, record):
        self.written.append(record)
        return True


class Adapter:
    def __init__(self, crash=False):
        self.crash = crash

    async def spawn(self, req):
        if self.crash:
            raise RuntimeError("boom")
        raise AssertionError("not used")

    async def reply(self, req):
        raise RuntimeError("boom")


def _runner(events=None, adapter=None, **kw):
    link, dispatched, notes = Link(), [], Notes()
    runner = EffectRunner(events=events or Events(), notes=notes, adapter=adapter or Adapter(), get_link=lambda: link,
                          get_state=HubState, dispatch=dispatched.append, **kw)  # fmt: skip
    return runner, link, dispatched, notes


def _item_effects():
    rec = records.queue_added(qid=None, project="cpa", text="t", intent="todo", origin="device", at=AT)
    return (fx.Append((rec,)), fx.Send(m.OutboxAck("3QZK1M7-0000C-0003", "stored")), fx.WriteNote({"ev": "x"}))


def test_append_then_ack_then_note_and_qids_assigned():
    runner, link, dispatched, notes = _runner()
    runner.run(_item_effects())
    assert link.sent == [("reply", m.OutboxAck("3QZK1M7-0000C-0003", "stored"))]
    assert isinstance(dispatched[0], inputs.Appended) and dispatched[0].records[0]["qid"] == "q1"
    assert notes.written == [{"ev": "x"}]


def test_lines_another_process_appended_are_absorbed_with_the_runners_own():
    """`cpa rec unhide` appends from another process; an append that reads it back hands it to Recordings too
    (docs/13 §8.4). The runner's own records go there as well: the filing fold (docs/23 §2.2) needs queue.spawned
    and queue.done to know whether a hand is still open, and no poll() sees them."""
    unhidden = {"ev": "rec.unhidden", "rec": "K3M9Q2T-00042-0A2F", "at": AT}
    absorbed = []
    runner, _, dispatched, _ = _runner(events=Events(others=(unhidden,)), absorb=absorbed.append)
    runner.run(_item_effects())
    assert [[e["ev"] for e in batch] for batch in absorbed] == [["rec.unhidden", "queue.added"]]
    assert dispatched[0].records == absorbed[0]
    runner, _, _, _ = _runner(absorb=absorbed.append)
    runner.run((fx.Append((records.queue_spawned("q1", AT),)),))
    assert absorbed[1] == (records.queue_spawned("q1", AT),)


def test_failed_append_sends_no_ack():
    runner, link, dispatched, notes = _runner(events=Events(fail=True))
    runner.run(_item_effects())
    assert link.sent == [] and dispatched == [] and notes.written == []


def test_push_goes_through_the_credit_path_and_is_dropped_without_a_ready_link():
    runner, link, _, _ = _runner()
    runner.run((fx.Push(m.Ping()),))
    assert link.sent == [("request", m.Ping())]
    link.ready = False
    runner.run((fx.Push(m.Ping()), fx.Send(m.Ack("d1"))))
    assert len(link.sent) == 1


async def test_agent_crashes_become_internal_errors():
    runner, _, dispatched, _ = _runner(adapter=Adapter(crash=True))
    runner.run((fx.RunSpawn("d7", SpawnRequest("/r", "cpa/x", "go", "plan"), "cpa", "q1"),
                fx.RunReply("d8", ReplyRequest("2a9c4f17", "go"))))  # fmt: skip
    await asyncio.sleep(0)
    await runner.drain()
    by_re = {d.re: d for d in dispatched}
    assert by_re["d7"].result.code == "E_INTERNAL" and by_re["d7"].qid == "q1"
    assert by_re["d8"].result.code == "E_INTERNAL" and (by_re["d8"].kind, by_re["d8"].sid) == ("reply", "2a9c4f17")
