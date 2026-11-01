"""Tests for debug.lang and debug.state.ui.lang (test firmware only, protocol README item 25, docs/21 §7).

debug.lang {lang: "zh" | "en"} switches the device's UI language the way the settings page does (and stores it); the
reply is ack. debug.state.ui gains an optional lang, the language shown now, so HIL can set it and put it back. Both
are additive: ui without lang is still valid, and the worst summary with it still fits line_max.
"""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
REVS = {"projects": 1, "sessions": 2, "queue": 3}
UINT32 = 4294967295


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def lang_request(**fields: object) -> dict:
    return {"v": 0, "t": "debug.lang", "id": "h90", **fields}


def state_reply(**extra: object) -> dict:
    return {
        "v": 0, "t": "debug.state", "id": "d71", "re": "h71", "up_ms": 9, "link": "Ready", "mod": "system",
        "overlay": "none", "outbox": {"pending": 0, "inflight": 0, "bad": 0}, "revs": REVS, "need": 0,
        "projects": [], **extra,
    }  # fmt: skip


@pytest.mark.parametrize("lang", ["zh", "en"])
def test_debug_lang_is_accepted(contract: schemas.Contract, lang: str) -> None:
    assert schemas.field_errors(contract, lang_request(lang=lang)) == ()


REJECTED = [({"lang": "fr"}, "lang"), ({"lang": "EN"}, "lang"), ({"lang": None}, "lang"), ({}, "lang")]


@pytest.mark.parametrize(("fields", "field"), REJECTED, ids=[repr(f) for f, _ in REJECTED])
def test_debug_lang_is_rejected_on_one_field(contract: schemas.Contract, fields: dict, field: str) -> None:
    assert schemas.field_errors(contract, lang_request(**fields)) == (field,)


def test_debug_lang_is_host_to_device_only(contract: schemas.Contract) -> None:
    assert contract.schemas["debug.lang"]["x-dir"] == ["h2d"]


@pytest.mark.parametrize("lang", ["zh", "en"])
def test_ui_lang_is_accepted(contract: schemas.Contract, lang: str) -> None:
    msg = state_reply(ui={"menu": False, "locked": False, "lang": lang})
    assert schemas.field_errors(contract, msg) == ()


def test_ui_without_lang_is_still_valid(contract: schemas.Contract) -> None:
    """A v0.4.0 build sends ui{menu, locked} only: lang is optional."""
    assert schemas.field_errors(contract, state_reply(ui={"menu": True, "locked": False})) == ()
    assert "lang" not in contract.schemas["debug.state"]["$defs"]["summary"]["properties"]["ui"]["required"]


def test_ui_lang_is_rejected(contract: schemas.Contract) -> None:
    msg = state_reply(ui={"menu": False, "locked": False, "lang": "fr"})
    assert schemas.field_errors(contract, msg) == ("ui.lang",)


def test_worst_summary_with_ui_lang_fits_line_max(contract: schemas.Contract) -> None:
    """test_debug_state_diag's worst line plus ui.lang: 12 bytes more."""
    health = {"ring_high": 65535} | dict.fromkeys(("mic_missing", "rot_ms_max", "heap_free", "heap_block"), UINT32)
    diag = dict.fromkeys(
        ("heap_free", "heap_min", "heap_block", "stack_left", "audio_stack_left", "loop_n", "loop_max_us",
         "loop_win_max_us", "gap_max_us", "gap_win_max_us", "loop_over_20", "loop_over_50", "loop_over_100",
         "loop_over_500"), UINT32,
    ) | {"ghost_n": 65535, "kb_reinit_n": 65535}  # fmt: skip
    project = {"run": 65535, "need": 65535, "queued": 65535}
    msg = {
        **state_reply(
            up_ms=UINT32, link="Detached", mod="a" * 16, overlay="compose", overlay_ms=UINT32, key_ms=UINT32,
            outbox=dict.fromkeys(("pending", "inflight", "bad"), 65535), revs=dict.fromkeys(REVS, UINT32),
            need=65535, projects=[{"slot": i, **project} for i in range(1, 10)],
            rec={"live": False, "mode": "meeting", "seg": 9999, "files_n": 256, "files_rev": UINT32, **health},
            ui={"menu": False, "locked": False, "lang": "zh"}, diag=diag,
        ),
        "id": "A" * 24, "re": "B" * 24,
    }  # fmt: skip
    assert schemas.field_errors(contract, msg) == ()
    line = wire.canonical_line(msg)
    assert len(line) == 1464 + len(',"lang":"zh"')
    assert len(line) <= contract.limits["line_max"]
