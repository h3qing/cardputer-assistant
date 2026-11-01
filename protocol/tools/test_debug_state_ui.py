"""Tests for the v0.4.0 debug.state.ui field (docs/13 §12.1): optional {menu, locked}; overlay keeps its values."""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]
REVS = {"projects": 1, "sessions": 2, "queue": 3}


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def state_reply(**extra: object) -> dict:
    return {
        "v": 0, "t": "debug.state", "id": "d71", "re": "h71", "up_ms": 9, "link": "Ready", "mod": "home",
        "overlay": "none", "outbox": {"pending": 0, "inflight": 0, "bad": 0}, "revs": REVS, "need": 0,
        "projects": [], **extra,
    }  # fmt: skip


@pytest.mark.parametrize(
    "ui",
    [
        {"menu": True, "locked": False},
        {"menu": False, "locked": True},
        {"menu": True, "locked": True, "later": 1},  # unknown fields stay allowed (additive rule)
        None,  # null: the same as absent, a build before v0.4.0
    ],
)
def test_ui_is_accepted(contract: schemas.Contract, ui: object) -> None:
    assert schemas.field_errors(contract, state_reply(ui=ui)) == ()


def test_reply_without_ui_is_accepted(contract: schemas.Contract) -> None:
    assert schemas.field_errors(contract, state_reply()) == ()


def test_menu_reports_the_layer_under_it(contract: schemas.Contract) -> None:
    msg = state_reply(overlay="compose", overlay_ms=8, ui={"menu": True, "locked": False})
    assert schemas.field_errors(contract, msg) == ()


REJECTED = [
    ({"menu": 1, "locked": False}, "ui.menu"),
    ({"menu": "yes", "locked": False}, "ui.menu"),
    ({"locked": False}, "ui.menu"),
    ({"menu": False, "locked": None}, "ui.locked"),
    ({"menu": False}, "ui.locked"),
    (True, "ui"),
    ("menu", "ui"),
]


@pytest.mark.parametrize(("ui", "field"), REJECTED, ids=[f"{u!r}:{f}" for u, f in REJECTED])
def test_ui_is_rejected_on_one_field(contract: schemas.Contract, ui: object, field: str) -> None:
    assert schemas.field_errors(contract, state_reply(ui=ui)) == (field,)


def test_overlay_values_are_unchanged(contract: schemas.Contract) -> None:
    """An old cpad accepts only none / compose / confirm (daemon specs.py), so menu and lock live in ui."""
    summary = contract.schemas["debug.state"]["$defs"]["summary"]
    assert summary["properties"]["overlay"] == {"enum": ["none", "compose", "confirm"]}
    assert schemas.field_errors(contract, state_reply(overlay="menu")) == ("overlay",)
    assert schemas.field_errors(contract, state_reply(overlay="lock")) == ("overlay",)


def test_ui_is_optional(contract: schemas.Contract) -> None:
    assert "ui" not in contract.schemas["debug.state"]["$defs"]["summary"]["required"]
