"""Tests for queue.snapshot items[].uncertain (docs/20 U25, protocol/README item 22): optional, boolean or null."""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def snapshot(**extra: object) -> dict:
    item = {"qid": "q18", "slot": 1, "text": "write native tests", "intent": "spawn", **extra}
    return {"v": 0, "t": "queue.snapshot", "id": "h5", "rev": 9, "items": [item]}


@pytest.mark.parametrize("value", [True, False, None])
def test_uncertain_is_accepted(contract: schemas.Contract, value: object) -> None:
    assert schemas.field_errors(contract, snapshot(uncertain=value)) == ()


def test_an_item_without_uncertain_is_accepted(contract: schemas.Contract) -> None:
    assert schemas.field_errors(contract, snapshot()) == ()


@pytest.mark.parametrize("value", ["yes", 1, 0, [], {}])
def test_uncertain_is_rejected_on_its_own_field(contract: schemas.Contract, value: object) -> None:
    assert schemas.field_errors(contract, snapshot(uncertain=value)) == ("items[0].uncertain",)


def test_uncertain_is_optional(contract: schemas.Contract) -> None:
    item = contract.schemas["queue.snapshot"]["properties"]["items"]["items"]
    assert "uncertain" in item["properties"] and "uncertain" not in item["required"]
