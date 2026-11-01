"""Tests for the device-side rec.* invariants (docs/13 §8.2, §12.2) enforced by flows.check_flow."""

from __future__ import annotations

from pathlib import Path

import flows
import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]
REC = "3QZK1M7-00009-0001"
CURSOR = "t1792440000000-a1"
REVS = {"projects": 0, "sessions": 0, "queue": 0}


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def host(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "host", "msg": msg, **extra}


def dev(t: int, msg: dict, **extra: object) -> dict:
    return {"t": t, "from": "device", "msg": msg, **extra}


def event(t: int, name: str) -> dict:
    return {"t": t, "event": name}


def host_hello(mid: str = "h1", recs: bool | None = True) -> dict:
    caps = {"spawn": True, "reply": False} if recs is None else {"spawn": True, "reply": False, "recs": recs}
    return {"v": 0, "t": "hello", "id": mid, "role": "host", "app": "cpad/0.4.1", "agent": "ok", "caps": caps}


def device_hello(mid: str = "d1", re: str = "h1") -> dict:
    return {
        "v": 0, "t": "hello", "id": mid, "re": re, "role": "device", "fw": "cpa-fw/0.4.1", "dev": "020000000001",
        "boot": "3QZK1M7-0000C", "up_ms": 1, "rst": "poweron", "time": False, "pending": 0, "bat_mv": 4100,
        "revs": REVS,
    }  # fmt: skip


def linked(recs: bool | None = True) -> tuple[dict, ...]:
    return (host(0, host_hello(recs=recs)), dev(10, device_hello()))


def rec_list(mid: str, cursor: str | None = None, max_: int = 6) -> dict:
    msg = {"v": 0, "t": "rec.list", "id": mid, "filter": "all", "max": max_}
    return {**msg, "cursor": cursor} if cursor else msg


def row(n: int) -> dict:
    return {"rec": f"3QZK1M7-00009-000{n}", "mode": "memo", "st": "done"}


def rec_items(mid: str, re: str, n: int = 1, next_: str | None = None) -> dict:
    msg = {"v": 0, "t": "rec.items", "id": mid, "re": re, "items": [row(i + 1) for i in range(n)]}
    return {**msg, "next": next_} if next_ else msg


def rec_act(mid: str, op: str = "hide") -> dict:
    return {"v": 0, "t": "rec.act", "id": mid, "rec": REC, "op": op}


def ack(mid: str, re: str) -> dict:
    return {"v": 0, "t": "ack", "id": mid, "re": re}


def error(mid: str, re: str, code: str = "E_UNKNOWN_ITEM") -> dict:
    return {"v": 0, "t": "error", "id": mid, "re": re, "code": code}


def errors(contract: schemas.Contract, *steps: dict) -> tuple[str, ...]:
    flow = {"name": "t", "spec": "test", "description": "test", "initial": {}, "steps": list(steps)}
    return flows.check_flow(contract, flow)


def one_error(contract: schemas.Contract, *steps: dict) -> str:
    problems = errors(contract, *steps)
    assert len(problems) == 1, problems
    return problems[0]


def paged() -> tuple[dict, ...]:
    return (*linked(), dev(100, rec_list("d2")), host(110, rec_items("h2", "d2", 6, CURSOR)))


# --- caps.recs gate ---------------------------------------------------------------


def test_list_and_act_after_caps_recs_are_clean(contract: schemas.Contract) -> None:
    steps = (*paged(), dev(200, rec_act("d3")), host(210, ack("h3", "d3")))
    assert errors(contract, *steps) == ()


@pytest.mark.parametrize("recs", [None, False])
def test_request_without_caps_recs_is_reported(contract: schemas.Contract, recs: bool | None) -> None:
    problem = one_error(contract, *linked(recs), dev(100, rec_list("d2")), host(110, rec_items("h2", "d2")))
    assert "d2" in problem and "caps.recs" in problem


def test_request_before_any_hello_is_reported(contract: schemas.Contract) -> None:
    problem = one_error(contract, dev(100, rec_act("d2")), host(110, ack("h2", "d2")))
    assert "d2" in problem and "caps.recs" in problem


def test_a_lost_hello_does_not_open_the_gate(contract: schemas.Contract) -> None:
    steps = (host(0, host_hello(), lost=True), dev(100, rec_act("d2")), host(110, ack("h2", "d2")))
    assert any("caps.recs" in p for p in errors(contract, *steps))


def test_a_later_hello_without_caps_recs_closes_the_gate(contract: schemas.Contract) -> None:
    steps = (*linked(), host(50, host_hello("h5", recs=None)), dev(60, device_hello("d5", "h5")))
    problem = one_error(contract, *steps, dev(100, rec_act("d6")), host(110, ack("h6", "d6")))
    assert "d6" in problem and "caps.recs" in problem


@pytest.mark.parametrize("name", ["detach", "device_reboot"])
def test_detach_and_reboot_forget_the_caps(contract: schemas.Contract, name: str) -> None:
    problem = one_error(contract, *linked(), event(50, name), dev(100, rec_act("d2")), host(110, ack("h2", "d2")))
    assert "caps.recs" in problem


def test_daemon_restart_is_invisible_to_the_device(contract: schemas.Contract) -> None:
    steps = (*linked(), event(50, "daemon_restart"), dev(100, rec_list("d2")))
    assert errors(contract, *steps, host(110, error("h1", "d2", "E_UNKNOWN_TYPE"))) == ()


# --- reply kinds ------------------------------------------------------------------


@pytest.mark.parametrize(
    "reply",
    [rec_items("h2", "d2"), error("h2", "d2", "E_BAD_FIELD"), error("h2", "d2", "E_UNKNOWN_TYPE")],
    ids=["rec.items", "E_BAD_FIELD", "E_UNKNOWN_TYPE"],
)
def test_rec_list_answers(contract: schemas.Contract, reply: dict) -> None:
    assert errors(contract, *linked(), dev(100, rec_list("d2")), host(110, reply)) == ()


def test_rec_list_answered_by_ack_is_reported(contract: schemas.Contract) -> None:
    problem = one_error(contract, *linked(), dev(100, rec_list("d2")), host(110, ack("h2", "d2")))
    assert "d2" in problem and "rec.list" in problem and "ack" in problem


@pytest.mark.parametrize("reply", [ack("h2", "d2"), error("h2", "d2")], ids=["ack", "E_UNKNOWN_ITEM"])
def test_rec_act_answers(contract: schemas.Contract, reply: dict) -> None:
    assert errors(contract, *linked(), dev(100, rec_act("d2")), host(110, reply)) == ()


def test_rec_act_answered_by_rec_items_is_reported(contract: schemas.Contract) -> None:
    problem = one_error(contract, *linked(), dev(100, rec_act("d2")), host(110, rec_items("h2", "d2")))
    assert "d2" in problem and "rec.act" in problem and "rec.items" in problem


def test_more_rows_than_asked_is_reported(contract: schemas.Contract) -> None:
    steps = (*linked(), dev(100, rec_list("d2", max_=2)), host(110, rec_items("h2", "d2", 3)))
    problem = one_error(contract, *steps)
    assert "d2" in problem and "3" in problem and "max 2" in problem


# --- cursor round trip -------------------------------------------------------------


def test_the_next_cursor_comes_back_verbatim(contract: schemas.Contract) -> None:
    steps = (*paged(), dev(200, rec_list("d3", CURSOR)), host(210, rec_items("h3", "d3", 2)))
    assert errors(contract, *steps) == ()


def test_a_made_up_cursor_is_reported(contract: schemas.Contract) -> None:
    steps = (*paged(), dev(200, rec_list("d3", "t1792440000000-a2")), host(210, rec_items("h3", "d3")))
    problem = one_error(contract, *steps)
    assert "d3" in problem and "cursor" in problem


def test_a_cursor_from_a_lost_page_is_reported(contract: schemas.Contract) -> None:
    steps = (*linked(), dev(100, rec_list("d2")), host(110, rec_items("h2", "d2", 6, CURSOR), lost=True))
    later = (host(4000, {"v": 0, "t": "ping", "id": "h9"}), dev(4010, rec_list("d3", CURSOR)))
    pong = {"v": 0, "t": "pong", "id": "d9", "re": "h9", "up_ms": 1, "pending": 0, "revs": REVS}
    problems = errors(contract, *steps, *later, dev(4020, pong), host(4030, rec_items("h3", "d3")))
    assert any("d3" in p and "cursor" in p for p in problems)


def test_rec_requests_share_the_single_misc_slot(contract: schemas.Contract) -> None:
    steps = (*linked(), dev(100, rec_list("d2")), dev(101, rec_act("d3")))
    problems = errors(contract, *steps, host(110, rec_items("h2", "d2")), host(111, ack("h3", "d3")))
    assert any("d3" in p and "in-flight cap" in p for p in problems)
