"""Tests for schema loading, custom keywords and limits consistency (schemas.py)."""

from __future__ import annotations

import copy
from pathlib import Path

import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def _project(slot: int, name: str = "博客") -> dict:
    return {"slot": slot, "key": f"p{slot}", "name": name, "run": 0, "need": 0, "queued": 0, "ok": True}


def _projects(*items: dict) -> dict:
    return {
        "v": 0,
        "t": "projects.snapshot",
        "id": "h3",
        "rev": 1,
        "projects": list(items),
        "other": {"run": 0, "n": 0},
    }


def test_contract_knows_every_catalog_type(contract: schemas.Contract) -> None:
    expected = {
        "hello",
        "ping",
        "pong",
        "ack",
        "error",
        "time.set",
        "projects.snapshot",
        "sessions.snapshot",
        "queue.snapshot",
        "outbox.item",
        "outbox.ack",
        "agent.spawn",
        "agent.reply",
        "agent.result",
        "queue.done",
        "inbox.dismiss",
        "debug.key",
        "debug.state",
        "debug.reboot",
        "debug.rec",
        "debug.mic",
        "debug.codec",
        "debug.lang",
        "file.list",
        "file.items",
        "file.get",
        "file.data",
        "file.del",
        "rec.list",
        "rec.items",
        "rec.act",
        "dests.snapshot",
    }
    assert set(contract.schemas) == expected


def test_limits_are_loaded(contract: schemas.Contract) -> None:
    assert contract.limits["line_max"] == 2048
    assert contract.limits["text_max"] == 500
    assert contract.limits["host_inflight_max"] == 4096


def test_valid_message_has_no_field_errors(contract: schemas.Contract) -> None:
    assert schemas.field_errors(contract, _projects(_project(1))) == ()


def test_max_bytes_counts_utf8_bytes_not_characters(contract: schemas.Contract) -> None:
    ok = _projects(_project(1, "八个汉字项目名称"))  # 8 chars, 24 bytes
    too_long = _projects(_project(1, "九个汉字的项目名称"))  # 9 chars, 27 bytes
    assert schemas.field_errors(contract, ok) == ()
    assert schemas.field_errors(contract, too_long) == ("projects[0].name",)


def test_unique_by_reports_the_duplicate_item(contract: schemas.Contract) -> None:
    msg = _projects(_project(1), _project(1))
    assert schemas.field_errors(contract, msg) == ("projects[1].slot",)


def test_max_per_key_reports_the_first_item_over_the_cap(contract: schemas.Contract) -> None:
    items = [{"qid": f"q{i}", "slot": 1, "text": "t", "intent": "todo"} for i in range(5)]
    msg = {"v": 0, "t": "queue.snapshot", "id": "h5", "rev": 1, "items": items}
    assert schemas.field_errors(contract, msg) == ("items[4].slot",)


def test_missing_nested_field_reports_its_path(contract: schemas.Contract) -> None:
    msg = {
        "v": 0,
        "t": "outbox.item",
        "id": "3QZK1M7-0000C-0003",
        "kind": "note",
        "at": {"up_ms": 1},
        "body": {},
    }
    assert schemas.field_errors(contract, msg) == ("body.text",)


def test_host_initiated_message_must_not_carry_re(contract: schemas.Contract) -> None:
    msg = {"v": 0, "t": "ping", "id": "h1", "re": "d1"}
    assert schemas.field_errors(contract, msg) == ("re",)


@pytest.mark.parametrize("text", ["-c", "--dangerously-skip-permissions", "  -c test", "a\u0000b", "", "a" * 501])
def test_agent_spawn_rejects_hostile_text(contract: schemas.Contract, text: str) -> None:
    msg = {"v": 0, "t": "agent.spawn", "id": "d7", "project": "cpa", "text": text}
    assert schemas.field_errors(contract, msg) == ("text",)


@pytest.mark.parametrize("text", ["$(rm -rf ~)", "; ls", "fix a-b", "a" * 500])
def test_agent_spawn_accepts_literal_text(contract: schemas.Contract, text: str) -> None:
    msg = {"v": 0, "t": "agent.spawn", "id": "d7", "project": "cpa", "text": text}
    assert schemas.field_errors(contract, msg) == ()


def test_optional_fields_accept_null(contract: schemas.Contract) -> None:
    msg = {"v": 0, "t": "agent.result", "id": "h7", "re": "d7", "ok": True, "name": None, "short": None, "code": None}
    assert schemas.field_errors(contract, msg) == ()


def _device_hello(**extra: object) -> dict:
    return {
        "v": 0, "t": "hello", "id": "d1", "re": "h1", "role": "device", "fw": "cpa-fw/0.2.0",
        "dev": "020000000001", "boot": "3QZK1M7-0000C", "up_ms": 53211, "rst": "software", "time": False,
        "pending": 3, "bat_mv": 4150, "revs": {"projects": 0, "sessions": 0, "queue": 0}, **extra,
    }  # fmt: skip


@pytest.mark.parametrize(
    "extra",
    [
        {},
        {"store": "sd", "launcher": True},
        {"store": "int", "launcher": False},
        {"store": "none"},
        {"launcher": True},
        {"store": None, "launcher": None},
    ],
)
def test_device_hello_store_and_launcher_are_optional(contract: schemas.Contract, extra: dict) -> None:
    assert schemas.field_errors(contract, _device_hello(**extra)) == ()


@pytest.mark.parametrize(
    ("extra", "field"),
    [
        ({"store": "fat"}, "store"),
        ({"store": "SD"}, "store"),
        ({"store": ""}, "store"),
        ({"store": 1}, "store"),
        ({"launcher": "yes"}, "launcher"),
        ({"launcher": 1}, "launcher"),
    ],
)
def test_device_hello_rejects_unknown_store_or_launcher(contract: schemas.Contract, extra: dict, field: str) -> None:
    assert schemas.field_errors(contract, _device_hello(**extra)) == (field,)


def test_limit_mismatches_empty_for_repo_contract(contract: schemas.Contract) -> None:
    assert schemas.limit_mismatches(contract) == ()


def test_limit_mismatches_detects_drift(contract: schemas.Contract) -> None:
    limits = copy.deepcopy(dict(contract.limits))
    limits["text_max"] = 499
    drifted = schemas.Contract(
        root=contract.root,
        limits=limits,
        common=contract.common,
        schemas=contract.schemas,
        validators=contract.validators,
    )
    problems = schemas.limit_mismatches(drifted)
    assert len(problems) == 1 and "text_max" in problems[0]
