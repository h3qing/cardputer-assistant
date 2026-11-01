"""Tests for flow invariants: one reply per host message, credit, in-flight caps."""

from __future__ import annotations

import base64
from pathlib import Path

import flows
import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
ITEM = "3QZK1M7-0000C-000"


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def host(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "host", "msg": msg, **extra}


def dev(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "device", "msg": msg, **extra}


def event(t: int, name: str) -> dict:
    return {"t": t, "event": name}


def expect(t: int, **values: object) -> dict:
    return {"t": t, "expect": {k.replace("__", "."): v for k, v in values.items()}}


def flow(*steps: dict) -> dict:
    return {"name": "t", "spec": "test", "description": "test", "initial": {}, "steps": list(steps)}


def ping(mid: str) -> dict:
    return {"v": 0, "t": "ping", "id": mid}


def ack(mid: str, re: str) -> dict:
    return {"v": 0, "t": "ack", "id": mid, "re": re}


def pong(mid: str, re: str) -> dict:
    revs = {"projects": 0, "sessions": 0, "queue": 0}
    return {"v": 0, "t": "pong", "id": mid, "re": re, "up_ms": 1, "pending": 0, "revs": revs}


def host_hello(mid: str) -> dict:
    caps = {"spawn": True, "reply": False}
    return {"v": 0, "t": "hello", "id": mid, "role": "host", "app": "cpad/0.1.0", "agent": "ok", "caps": caps}


def item(n: int) -> dict:
    return {
        "v": 0,
        "t": "outbox.item",
        "id": f"{ITEM}{n}",
        "kind": "note",
        "at": {"up_ms": 1, "wall_ms": None},
        "body": {"text": f"note {n}"},
    }


def outbox_ack(mid: str, n: int) -> dict:
    return {"v": 0, "t": "outbox.ack", "id": mid, "re": f"{ITEM}{n}", "status": "stored"}


def spawn(mid: str) -> dict:
    return {"v": 0, "t": "agent.spawn", "id": mid, "project": "cpa", "text": "write tests"}


def padded(mid: str, size: int) -> dict:
    base = {"v": 0, "t": "queue.snapshot", "id": mid, "rev": 1, "items": [], "x_pad": ""}
    pad = size - len(wire.canonical_line(base))
    return {**base, "x_pad": "a" * pad}


def size(msg: dict) -> int:
    return len(wire.canonical_line(msg))


def errors(contract: schemas.Contract, *steps: dict) -> tuple[str, ...]:
    return flows.check_flow(contract, flow(*steps))


# --- one reply per host-initiated message ------------------------------------


def test_complete_exchange_has_no_errors(contract: schemas.Contract) -> None:
    assert errors(contract, host(0, ping("h1")), dev(10, pong("d1", "h1"))) == ()


def test_missing_reply_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, host(0, ping("h2")))
    assert len(problems) == 1 and "h2" in problems[0]


def test_double_reply_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, host(0, ping("h1")), dev(5, pong("d1", "h1")), dev(6, pong("d2", "h1")))
    assert any("h1" in p for p in problems)


@pytest.mark.parametrize("name", ["detach", "device_reboot", "daemon_restart"])
def test_reset_events_excuse_unanswered_messages(contract: schemas.Contract, name: str) -> None:
    assert errors(contract, host(0, ping("h1")), event(10, name)) == ()


def test_lost_host_line_needs_no_reply(contract: schemas.Contract) -> None:
    assert errors(contract, host(0, ping("h1"), lost=True)) == ()


def test_reply_to_unknown_host_message_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, dev(0, ack("d1", "h99")))
    assert len(problems) == 1 and "h99" in problems[0]


def test_host_reply_needs_a_delivered_device_line(contract: schemas.Contract) -> None:
    problems = errors(contract, dev(0, item(1), lost=True), host(10, outbox_ack("h1", 1)))
    assert any(f"{ITEM}1" in p for p in problems)


# --- credit window -------------------------------------------------------------


def test_credit_expectation_matches_bytes_in_flight(contract: schemas.Contract) -> None:
    n = size(ping("h1"))
    steps = (
        host(0, ping("h1")),
        expect(0, host__credit_used=n),
        dev(5, pong("d1", "h1")),
        expect(5, host__credit_used=0),
    )
    assert errors(contract, *steps) == ()


def test_wrong_credit_expectation_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, host(0, ping("h1")), expect(0, host__credit_used=1), dev(5, pong("d1", "h1")))
    assert len(problems) == 1 and "credit" in problems[0]


def test_parked_reply_releases_credit_on_delivery(contract: schemas.Contract) -> None:
    n = size(ping("h1"))
    steps = (
        host(0, ping("h1")),
        dev(10, pong("d1", "h1"), deliver_t=2000),
        expect(1999, host__credit_used=n),
        expect(2000, host__credit_used=0),
    )
    assert errors(contract, *steps) == ()


def test_credit_is_reclaimed_after_timeout(contract: schemas.Contract) -> None:
    n = size(ping("h1"))
    steps = (host(0, ping("h1"), lost=True), expect(4999, host__credit_used=n), expect(5000, host__credit_used=0))
    assert errors(contract, *steps) == ()


def test_credit_overflow_is_reported(contract: schemas.Contract) -> None:
    steps = (
        host(0, padded("h1", 1900)),
        host(0, padded("h2", 1900)),
        host(0, padded("h3", 1900)),
        dev(10, ack("d1", "h1")),
        dev(10, ack("d2", "h2")),
        dev(10, ack("d3", "h3")),
    )
    problems = errors(contract, *steps)
    assert len(problems) == 1 and "h3" in problems[0] and "credit" in problems[0]


# --- device in-flight caps -----------------------------------------------------


def test_fifth_outbox_item_in_flight_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, *(dev(i, item(i)) for i in range(1, 6)))
    assert len(problems) == 1 and f"{ITEM}5" in problems[0]


def test_retransmission_does_not_count_twice(contract: schemas.Contract) -> None:
    steps = (*(dev(i, item(i)) for i in range(1, 5)), dev(3000, item(1)), expect(3000, device__outbox_inflight=4))
    assert errors(contract, *steps) == ()


def test_outbox_ack_releases_slot(contract: schemas.Contract) -> None:
    steps = (dev(0, item(1)), host(5, outbox_ack("h1", 1)), expect(5, device__outbox_inflight=0))
    assert errors(contract, *steps) == ()


def test_host_hello_returns_in_flight_items(contract: schemas.Contract) -> None:
    hello_reply = {
        "v": 0,
        "t": "hello",
        "id": "d9",
        "re": "h5",
        "role": "device",
        "fw": "cpa-fw/0.1.0",
        "dev": "020000000001",
        "boot": "3QZK1M7-0000C",
        "up_ms": 9,
        "rst": "poweron",
        "time": False,
        "pending": 2,
        "bat_mv": 4100,
        "revs": {"projects": 0, "sessions": 0, "queue": 0},
    }
    steps = (
        dev(0, item(1)),
        dev(0, item(2)),
        host(10, host_hello("h5")),
        expect(10, device__outbox_inflight=0),
        dev(20, hello_reply),
    )
    assert errors(contract, *steps) == ()


def test_second_agent_request_in_flight_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, dev(0, spawn("d1")), dev(10, spawn("d2")))
    assert len(problems) == 1 and "d2" in problems[0]


def test_agent_request_expires_after_timeout(contract: schemas.Contract) -> None:
    steps = (
        dev(0, spawn("d1")),
        expect(29999, device__agent_inflight=1),
        expect(30000, device__agent_inflight=0),
        dev(30000, spawn("d2")),
    )
    assert errors(contract, *steps) == ()


# --- step validation -----------------------------------------------------------


def test_raw_noise_line_needs_declared_outcome(contract: schemas.Contract) -> None:
    raw = base64.b64encode(b"ESP-ROM:esp32s3\n").decode()
    undeclared = {"t": 0, "from": "device", "line_b64": raw}
    declared = {**undeclared, "outcome": "noise"}
    assert len(errors(contract, undeclared)) == 1
    assert errors(contract, declared) == ()


def test_invalid_message_is_reported(contract: schemas.Contract) -> None:
    bad = {"v": 0, "t": "time.set", "id": "h1", "epoch_ms": 1, "tz_min": 0}
    problems = errors(contract, host(0, bad), dev(1, ack("d1", "h1")))
    assert len(problems) == 1 and "epoch_ms" in problems[0]


def test_wrong_direction_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, host(0, pong("h1", "d1")))
    assert any("direction" in p for p in problems)


def test_time_going_backwards_is_reported(contract: schemas.Contract) -> None:
    problems = errors(contract, host(10, ping("h1")), dev(5, pong("d1", "h1")))
    assert any("time" in p for p in problems)


def test_parking_is_only_for_device_lines(contract: schemas.Contract) -> None:
    problems = errors(contract, host(0, ping("h1"), deliver_t=10), dev(20, pong("d1", "h1")))
    assert any("deliver_t" in p for p in problems)


def test_declarative_expectations_are_not_simulated(contract: schemas.Contract) -> None:
    assert errors(contract, expect(0, device__link="Ready", host__items_stored=3)) == ()
