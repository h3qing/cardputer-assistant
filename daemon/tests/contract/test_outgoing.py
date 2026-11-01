"""Everything cpad sends is valid under the reference schema checker (protocol/tools), whatever the input.

Snapshot content comes from session names, project names and queue texts the daemon does not control,
so it is generated with hypothesis (control characters, CJK, quotes, backslashes, huge strings).
Host replies to device requests must stay <= host_reply_max (256 B): 05 §6.3's credit math relies on it.
The one exception is rec.items (v0.4.1, protocol/README): a full page is bounded by line_max instead.
"""

from __future__ import annotations

from dataclasses import replace

import pytest
from hypothesis import HealthCheck, given, settings
from hypothesis import strategies as st

from cpad.agents.base import SessionInfo
from cpad.agents.claude_code.argv import session_name
from cpad.config import host
from cpad.events import records
from cpad.hub.snapshots import publish
from cpad.hub.state import AgentInfo, HubState
from cpad.inbox.need import Tracked
from cpad.projects.registry import Project
from cpad.protocol import codec
from cpad.protocol import messages as m
from cpad.protocol import rec_messages as r
from cpad.protocol.limits import LIMITS
from cpad.protocol.specs import ERROR_CODES
from cpad.queue.fold import fold_queue

WORST_ID = "h" + "9" * 23
AT = "2026-10-19T17:00:00.000+00:00"
nasty = st.text(alphabet=st.sampled_from('ab会议"\\\t\n\u0000\u007f　-/ '), min_size=0, max_size=60)


def _check(reference, msg) -> bytes:
    schemas, contract = reference
    wire = m.to_wire(replace(msg, id=WORST_ID))
    assert schemas.field_errors(contract, wire) == (), wire
    line = codec.encode(wire)
    assert codec.decode_line(line).ok
    return line


@settings(max_examples=60, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(names=st.lists(nasty, min_size=1, max_size=14), texts=st.lists(nasty, max_size=20))
def test_snapshots_from_hostile_content_are_valid(reference_contract, names, texts):
    projects = tuple(Project(f"p{i}", n or "x", i + 1, f"/r/{i}") for i, n in enumerate(names[:9]))
    tracked = tuple(
        Tracked(SessionInfo(f"{i:08x}-0000", f"{i:08x}", f"{i:08x}", "bg", "idle", None, n, "/r"), f"p{i % 9}", 0.0)
        for i, n in enumerate(names)
    )
    events = [records.queue_added(qid=f"q{i}", project=f"p{i % 3}", text=t or "t", intent="todo", origin="cli", at=AT)
              for i, t in enumerate(texts, start=1)]  # fmt: skip
    state = HubState(projects=projects, tracked=tracked, queue=fold_queue(events))
    _, snaps, _ = publish(state, now=100.0)
    for snap in (snaps.projects, snaps.sessions, snaps.queue):
        assert len(_check(reference_contract, snap)) <= LIMITS["line_max"]


def test_a_full_queue_of_uncertain_spawns_is_valid_and_fits(reference_contract):
    """docs/20 U25: items[].uncertain (protocol/README item 22) on all 12 rows with 80-byte CJK texts."""
    projects = tuple(Project(f"p{i}", "x", i + 1, f"/r/{i}") for i in range(3))
    events = [records.queue_added(qid=f"q{i}", project=f"p{i % 3}", text="会" * 40, intent="spawn", origin="device",
                                  at=AT, uncertain=True) for i in range(1, 13)]  # fmt: skip
    _, snaps, _ = publish(HubState(projects=projects, queue=fold_queue(events)), now=1.0)
    assert len(snaps.queue.items) == 12 and all(i.uncertain for i in snaps.queue.items)
    assert len(_check(reference_contract, snaps.queue)) <= LIMITS["line_max"]


@pytest.mark.parametrize("code", ERROR_CODES)
def test_worst_case_replies_fit_host_reply_max(reference_contract, code):
    monoid = "ZZZZZZZ-ZZZZZ-ZZZZ"
    name = session_name("abcdefghijkl", "w" * 40 + " x y z", 4294967295)
    replies = [
        m.OutboxAck(re=monoid, status="rejected", code=code),
        m.AgentResult(re="d" + "9" * 23, ok=False, code=code),
        m.AgentResult(re="d" + "9" * 23, ok=True, name=name, short="~" * 16),
        m.ErrorMsg(re="d" + "9" * 23, code=code, field="a" * 48),
        m.Ack(re="d" + "9" * 23),
    ]
    for reply in replies:
        assert len(_check(reference_contract, reply)) <= LIMITS["host_reply_max"]


def test_host_hello_and_time_set_are_valid(reference_contract):
    for agent in (AgentInfo(), AgentInfo(present=True, reply=True), AgentInfo(present=True, errored=True)):
        _check(reference_contract, m.HostHello(app="cpad/0.1.0", agent=agent.status, caps=agent.caps))
    _check(reference_contract, m.TimeSet(epoch_ms=1792440000000, tz_min=-420))
    _check(reference_contract, m.Ping())


def test_v041_host_hello_is_valid(reference_contract):
    caps = m.Caps(spawn=True, reply=True, recs=True, todo=True)
    longest = m.HostHello(app="cpad/0.4.1", agent="ok", caps=caps, host_id="z" * 32, host_label="八个汉字的主机名")
    _check(reference_contract, longest)
    _check(reference_contract, replace(longest, host_label=None))


@settings(max_examples=200, suppress_health_check=[HealthCheck.function_scoped_fixture])
@given(name=st.text(max_size=30) | nasty)
def test_every_name_cpa_host_name_accepts_makes_a_valid_host_hello(reference_contract, name):
    """docs/23 §4.2: the device refuses a whole host hello with a bad host_label, so the CLI checks first."""
    if host.valid_name(name):
        caps = m.Caps(spawn=True, reply=True, recs=True, todo=True)
        _check(reference_contract, m.HostHello(app="cpad/0.4.1", agent="ok", caps=caps, host_label=name))


def test_worst_case_rec_items_page_fits_line_max(reference_contract):
    """6 rows, 48-byte CJK titles, every optional field at its widest, a 32-character cursor (docs/13 §12.2)."""
    rows = tuple(
        r.RecRow(rec=f"ZZZZZZZ-ZZZZZ-ZZZ{i}", mode="meeting", st="processing", wall_ms=9007199254740991,
                 dur_ms=4294967295, project="abcdefghijkl", starred=True, marks=65535, title="录" * 16, todo_n=65535)
        for i in range(LIMITS["rec_list_max"])
    )  # fmt: skip
    page = r.RecItems(re="d" + "9" * 23, items=rows, next="z" * LIMITS["rec_cursor_max"])
    assert len(_check(reference_contract, page)) <= LIMITS["line_max"]
