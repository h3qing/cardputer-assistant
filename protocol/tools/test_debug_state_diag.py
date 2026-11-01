"""Tests for debug.state.diag (test firmware only, protocol README item 21, gh #21): optional device health counters.

diag is absent or null on a build without it; when present every field is required: heap and stack bytes, loop
timing in microseconds and counts (uint32, the loop counters wrap), ghost_n and kb_reinit_n (count). Like ui it
leaves the existing fields alone, and the worst summary with it still fits line_max.
"""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
REVS = {"projects": 1, "sessions": 2, "queue": 3}
UINT32 = 4294967295
DIAG = {
    "heap_free": 183028, "heap_min": 177060, "heap_block": 163828, "stack_left": 5100, "audio_stack_left": 2256,
    "loop_n": 52448550, "loop_max_us": 107993, "loop_win_max_us": 49535, "gap_max_us": 108007,
    "gap_win_max_us": 49550, "loop_over_20": 6711, "loop_over_50": 2, "loop_over_100": 1, "loop_over_500": 0,
    "ghost_n": 0, "kb_reinit_n": 0,
}  # fmt: skip
COUNTS = ("ghost_n", "kb_reinit_n")
HEALTH = {"ring_high": 65535, "mic_missing": UINT32, "rot_ms_max": UINT32, "heap_free": UINT32, "heap_block": UINT32}


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def state_reply(**extra: object) -> dict:
    return {
        "v": 0, "t": "debug.state", "id": "d71", "re": "h71", "up_ms": 9, "link": "Ready", "mod": "home",
        "overlay": "none", "outbox": {"pending": 0, "inflight": 0, "bad": 0}, "revs": REVS, "need": 0,
        "projects": [], **extra,
    }  # fmt: skip


def diag_max() -> dict:
    return {k: 65535 if k in COUNTS else UINT32 for k in DIAG}


@pytest.mark.parametrize(
    "diag",
    [
        DIAG,
        dict.fromkeys(DIAG, 0),  # a platform without loop timing reports zeros
        diag_max(),
        {**DIAG, "later": 1},  # unknown fields stay allowed (additive rule)
        None,  # null: the same as absent, a build without diag
    ],
)
def test_diag_is_accepted(contract: schemas.Contract, diag: object) -> None:
    assert schemas.field_errors(contract, state_reply(diag=diag)) == ()


def test_diag_is_optional(contract: schemas.Contract) -> None:
    assert "diag" not in contract.schemas["debug.state"]["$defs"]["summary"]["required"]
    assert schemas.field_errors(contract, state_reply()) == ()


def test_every_diag_field_is_required(contract: schemas.Contract) -> None:
    diag = contract.schemas["debug.state"]["$defs"]["summary"]["properties"]["diag"]
    assert diag["required"] == list(DIAG)
    assert list(diag["properties"]) == list(DIAG)


def without(name: str) -> dict:
    return {k: v for k, v in DIAG.items() if k != name}


REJECTED = [
    *((without(name), f"diag.{name}") for name in ("heap_free", "loop_n", "kb_reinit_n")),
    ({**DIAG, "heap_min": -1}, "diag.heap_min"),
    ({**DIAG, "loop_max_us": UINT32 + 1}, "diag.loop_max_us"),
    ({**DIAG, "loop_over_50": 1.5}, "diag.loop_over_50"),
    ({**DIAG, "stack_left": "5100"}, "diag.stack_left"),
    ({**DIAG, "ghost_n": 65536}, "diag.ghost_n"),
    ({**DIAG, "audio_stack_left": None}, "diag.audio_stack_left"),
    (True, "diag"),
    ([], "diag"),
]


@pytest.mark.parametrize(("diag", "field"), REJECTED, ids=[f"{f}#{i}" for i, (_, f) in enumerate(REJECTED)])
def test_diag_is_rejected_on_one_field(contract: schemas.Contract, diag: object, field: str) -> None:
    assert schemas.field_errors(contract, state_reply(diag=diag)) == (field,)


def test_diag_leaves_rec_health_and_ui_alone(contract: schemas.Contract) -> None:
    rec = {"live": False, "mode": None, "seg": None, "files_n": 0, "files_rev": 21, **HEALTH}
    msg = state_reply(rec=rec, ui={"menu": False, "locked": True}, diag=DIAG)
    assert schemas.field_errors(contract, msg) == ()


def test_worst_summary_with_diag_fits_line_max(contract: schemas.Contract) -> None:
    """9 projects, rec with every health counter, ui and diag, every number at its schema maximum."""
    project = {"run": 65535, "need": 65535, "queued": 65535}
    msg = {
        **state_reply(
            up_ms=UINT32, link="Detached", mod="a" * 16, overlay="compose", overlay_ms=UINT32, key_ms=UINT32,
            outbox=dict.fromkeys(("pending", "inflight", "bad"), 65535), revs=dict.fromkeys(REVS, UINT32),
            need=65535, projects=[{"slot": i, **project} for i in range(1, 10)],
            rec={"live": False, "mode": "meeting", "seg": 9999, "files_n": 256, "files_rev": UINT32, **HEALTH},
            ui={"menu": False, "locked": False}, diag=diag_max(),
        ),
        "id": "A" * 24, "re": "B" * 24,
    }  # fmt: skip
    assert schemas.field_errors(contract, msg) == ()
    line = wire.canonical_line(msg)
    assert len(line) <= contract.limits["line_max"]
    assert len(line) == 1464
