"""Replay protocol/vectors/v0/flows (with rec/) through cpad's credit window and reboot detector (05 §6.3, §6.4)."""

from __future__ import annotations

import json
from dataclasses import replace

import pytest

from cpad.device.credit import Credit
from cpad.device.handshake import Seen, is_reboot
from cpad.hub import effects as fx
from cpad.hub import inputs
from cpad.hub.reduce import reduce
from cpad.hub.state import AgentInfo, HubState
from cpad.projects.registry import Project
from cpad.protocol import codec
from cpad.protocol import messages as m
from tests.support.protocol_files import VECTORS, flows, raw_line

# flows/rec/ holds the flows in which the host asks for files, flows/app/ the recordings app's rec.* (v0.4.1)
# (protocol/README.md); the host side replays all of them.
REC_FLOWS = sorted((VECTORS / "flows" / "rec").glob("*.json"))
APP_FLOWS = sorted((VECTORS / "flows" / "app").glob("*.json"))
FLOWS = flows() + [json.loads(p.read_text(encoding="utf-8")) for p in (*REC_FLOWS, *APP_FLOWS)]


def _timeline(flow: dict) -> list[tuple[float, int, str, dict]]:
    """(time, order, kind, step): device lines count at their delivery time (TX parking)."""
    events = []
    for index, step in enumerate(flow["steps"]):
        if "from" in step:
            when = step.get("deliver_t", step["t"]) if step["from"] == "device" else step["t"]
            events.append((when, index, "line", step))
        elif "event" in step:
            events.append((step["t"], index, "event", step))
        else:
            events.append((step["t"], index, "expect", step))
    return sorted(events, key=lambda e: (e[0], e[1]))


def _host_side(credit: Credit, step: dict, now_s: float) -> Credit:
    decoded = codec.decode_line(raw_line(step))
    if step["from"] == "host" and decoded.reply_id is not None:
        return credit.sent(decoded.reply_id, len(raw_line(step)), now_s)
    if step["from"] == "device" and decoded.ok and "re" in decoded.msg and not step.get("lost"):
        return credit.answered(decoded.msg["re"])
    return credit


def _reboot(seen: Seen | None, step: dict) -> tuple[Seen | None, bool | None]:
    decoded = codec.decode_line(raw_line(step))
    if step["from"] != "device" or not decoded.ok or decoded.msg["t"] not in ("hello", "pong"):
        return seen, None
    msg = decoded.msg
    detected = is_reboot(seen, msg.get("boot"), msg["up_ms"])
    boot = msg.get("boot") or (seen.boot if seen else None)
    return Seen(boot, msg["up_ms"]), detected


@pytest.mark.parametrize("flow", FLOWS, ids=[f["name"] for f in FLOWS])
def test_flow_host_credit_and_reboot_expectations(flow):
    credit, seen, reboot, checked = Credit(), None, None, 0
    for when, _, kind, step in _timeline(flow):
        now_s = when / 1000
        if kind == "line":
            credit = _host_side(credit, step, now_s)
            seen, detected = _reboot(seen, step)
            reboot = reboot if detected is None else detected
        elif kind == "event" and step["event"] in ("detach", "daemon_restart"):
            credit = Credit()
        elif kind == "expect":
            expect = step["expect"]
            if "host.credit_used" in expect:
                assert credit.used(now_s) == expect["host.credit_used"], (flow["name"], step)
                checked += 1
            if "host.reboot_detected" in expect:
                assert reboot == expect["host.reboot_detected"], (flow["name"], step)
                checked += 1
    assert checked or flow["name"] in {"agent_result_unknown", "offline_spawn_becomes_queue", "outbox_replay_dup_ack"}


def test_the_file_flows_in_flows_rec_are_replayed_too():
    assert len(REC_FLOWS) >= 5 and {p.stem for p in REC_FLOWS} <= {f["name"] for f in FLOWS}


def test_the_recordings_app_flows_in_flows_app_are_replayed_too():
    assert len(APP_FLOWS) >= 3 and {p.stem for p in APP_FLOWS} <= {f["name"] for f in FLOWS}


APP_FLOW_DOCS = [f for f in FLOWS if f["name"] in {p.stem for p in APP_FLOWS}]


@pytest.mark.parametrize("flow", APP_FLOW_DOCS, ids=lambda f: f["name"])
def test_app_flow_lines_decode_as_rec_messages(flow):
    """Every rec.* line of the app flows is valid for cpad and parses to its dataclass."""
    lines = [s for s in flow["steps"] if "msg" in s and s["msg"]["t"].startswith("rec.")]
    assert lines
    for step in lines:
        decoded = codec.decode_line(raw_line(step))
        assert decoded.ok, (flow["name"], step["t"], decoded)
        assert type(m.from_wire(decoded.msg)).__name__ in {"RecList", "RecAct", "RecItems"}


def test_credit_window_flow_holds_the_queue_snapshot():
    flow = next(f for f in FLOWS if f["name"] == "credit_window")
    host = [s for s in flow["steps"] if s.get("from") == "host"]
    credit = Credit()
    for step in host[:2]:
        credit = credit.sent(codec.decode_line(raw_line(step)).reply_id, len(raw_line(step)), 0.0)
    queue_line = next(s for s in host if s["msg"]["t"] == "queue.snapshot")
    assert not credit.can_send(len(raw_line(queue_line)), 0.0)


def _host_counts(flow: dict) -> list[tuple[dict, dict]]:
    """Replay delivered device requests through hub.reduce with a persistent (dev, id) index."""
    state = HubState(projects=(Project("cpa", "cpa", 1, "/r"),), agent=AgentInfo(present=True), dev="020000000001")
    seen, counts, checks = set(), {"host.items_stored": 0, "host.queue_added": 0, "host.spawn_calls": 0}, []
    for when, _, kind, step in _timeline(flow):
        if kind == "expect":
            checks.append((step, dict(counts)))
            continue
        if kind != "line" or step["from"] != "device" or step.get("lost"):
            continue
        decoded = codec.decode_line(raw_line(step))
        if not decoded.ok or decoded.reply_id is None:
            continue
        msg = m.from_wire(decoded.msg)
        key = (state.dev, msg.id)
        state, effects = reduce(state, inputs.DeviceRequest(msg, when / 1000, 0.0, duplicate=key in seen))
        appended = [r for e in effects if isinstance(e, fx.Append) for r in e.records]
        seen |= {key} if any(r["ev"] == "item.received" for r in appended) else set()
        counts["host.items_stored"] += sum(r["ev"] == "item.received" for r in appended)
        counts["host.queue_added"] += sum(r["ev"] == "queue.added" for r in appended)
        counts["host.spawn_calls"] += sum(isinstance(e, fx.RunSpawn) for e in effects)
        state = replace(state, agent_busy=None)
    return checks


@pytest.mark.parametrize("flow", FLOWS, ids=[f["name"] for f in FLOWS])
def test_flow_host_item_and_spawn_counts(flow):
    for step, counts in _host_counts(flow):
        for key, want in step["expect"].items():
            if key in counts:
                assert counts[key] == want, (flow["name"], step["t"], key)
