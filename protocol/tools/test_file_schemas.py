"""Tests for the v0.3 recording-transfer schemas (docs/11-m3-design.md §5): file.*, debug.rec, files, rec, limits."""

from __future__ import annotations

import base64
import math
from pathlib import Path

import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]
FID = "3QZK1M7-00042-0A1C"
SHA = "0123456789abcdef" * 4
REVS = {"projects": 1, "sessions": 2, "queue": 3}
FILES = {"n": 2, "rev": 17, "live": False}


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def item(**extra: object) -> dict:
    base = {
        "id": FID, "kind": "rec", "size": 384044, "rec": FID, "seg": 0, "last": True, "mode": "memo",
        "dur_ms": 12000, "at": {"up_ms": 435000, "wall_ms": None},
    }  # fmt: skip
    return {**base, **extra}


def items_msg(*entries: dict, **extra: object) -> dict:
    return {"v": 0, "t": "file.items", "id": "d41", "re": "h41", "items": list(entries), "more": False, **extra}


def data_msg(payload: bytes, **extra: object) -> dict:
    b64 = base64.b64encode(payload).decode() if payload else None
    base = {"v": 0, "t": "file.data", "id": "d42", "re": "h42", "fid": FID, "off": 0, "n": len(payload)}
    return {**base, "size": 384044, "crc": 0, "b64": b64, **extra}


def get_msg(**extra: object) -> dict:
    return {"v": 0, "t": "file.get", "id": "h42", "fid": FID, "off": 0, "len": 1380, **extra}


def del_msg(**extra: object) -> dict:
    return {"v": 0, "t": "file.del", "id": "h99", "fid": FID, "size": 384044, "sha256": SHA, **extra}


def pong(**extra: object) -> dict:
    return {"v": 0, "t": "pong", "id": "d40", "re": "h40", "up_ms": 1, "pending": 0, "revs": REVS, **extra}


def device_hello(**extra: object) -> dict:
    return {
        "v": 0, "t": "hello", "id": "d1", "re": "h1", "role": "device", "fw": "cpa-fw/0.3.0",
        "dev": "020000000001", "boot": "3QZK1M7-0000C", "up_ms": 53211, "rst": "software", "time": False,
        "pending": 0, "bat_mv": 4150, "revs": REVS, "store": "sd", "launcher": False, **extra,
    }  # fmt: skip


REC_IDLE = {"live": False, "mode": None, "seg": None, "files_n": 0, "files_rev": 21}
HEALTH = {"ring_high": 2, "mic_missing": 0, "rot_ms_max": 214, "heap_free": 171520, "heap_block": 155648}


def state_reply(**extra: object) -> dict:
    return {
        "v": 0, "t": "debug.state", "id": "d71", "re": "h71", "up_ms": 9, "link": "Ready", "mod": "home",
        "overlay": "none", "outbox": {"pending": 0, "inflight": 0, "bad": 0}, "revs": REVS, "need": 0,
        "projects": [], **extra,
    }  # fmt: skip


# --- limits ----------------------------------------------------------------------

NEW_LIMITS = {
    "file_list_max": 4,
    "file_get_b64_max": 1380,
    "file_inflight_max": 4,
    "file_pending_max": 256,
    "file_marks_max": 16,
    "file_seg_max": 9999,
    "sha256_pattern": "^[0-9a-f]{64}$",
    "lead_ms_max": 2000,
    "rec_min_ms": 1000,
    "memo_max_ms": 300000,
    "meeting_max_ms": 10800000,
    "seg_ms_default": 300000,
    "seg_ms_min": 10000,
    "seg_ms_max": 600000,
}
MIRRORED = (
    "file_list_max",
    "file_get_b64_max",
    "file_pending_max",
    "file_marks_max",
    "file_seg_max",
    "sha256_pattern",
    "lead_ms_max",
    "seg_ms_min",
    "seg_ms_max",
)


def test_limits_json_has_every_new_key(contract: schemas.Contract) -> None:
    assert {k: contract.limits.get(k) for k in NEW_LIMITS} == NEW_LIMITS


def test_schema_bounds_are_tied_to_limits(contract: schemas.Contract) -> None:
    assert set(MIRRORED) <= {key for key, _, _ in schemas.LIMIT_POINTERS}
    assert schemas.limit_mismatches(contract) == ()


def test_derived_bounds_follow_the_limits(contract: schemas.Contract) -> None:
    defs, limits = contract.common["$defs"], contract.limits
    assert defs["fileSize"]["maximum"] == 44 + limits["seg_ms_max"] * 32  # 16 kHz, 16 bit, mono
    assert defs["fileItem"]["properties"]["dur_ms"]["maximum"] == limits["seg_ms_max"]
    assert defs["fileItem"]["properties"]["stars"]["items"]["exclusiveMaximum"] == limits["seg_ms_max"]
    b64 = contract.schemas["file.data"]["$defs"]["b64"]
    assert b64["maxLength"] == 4 * math.ceil(limits["file_get_b64_max"] / 3)


# --- accepted -------------------------------------------------------------------

FULL_ITEM = item(
    sha256=SHA, project="cpa", slot=1, stars=[0, 599999], lead_ms=2000, consent="ack", rcv=False, mode="meeting"
)


@pytest.mark.parametrize(
    "msg",
    [
        {"v": 0, "t": "file.list", "id": "h41"},
        {"v": 0, "t": "file.list", "id": "h41", "after": FID, "max": 4},
        {"v": 0, "t": "file.list", "id": "h41", "after": None, "max": None},
        items_msg(),
        items_msg(item(), FULL_ITEM, item(at=None, sha256=None, project=None, slot=None), item(), more=True),
        get_msg(),
        get_msg(off=4294967295, len=1),
        get_msg(enc="bin"),
        data_msg(b"RIFF"),
        data_msg(b"\x00" * 1380, off=1380),
        data_msg(b"", off=384044),
        del_msg(),
        del_msg(sha256=None),
        {"v": 0, "t": "debug.rec", "id": "h80", "seg_ms": 10000},
        {"v": 0, "t": "debug.rec", "id": "h80", "seg_ms": 600000},
        pong(),
        pong(files=FILES),
        pong(files={"n": 256, "rev": 0, "live": True}),
        pong(files=None),
        device_hello(files=FILES),
        state_reply(rec={"live": True, "mode": "meeting", "seg": 3, "files_n": 2, "files_rev": 17}),
        state_reply(rec={"live": False, "mode": None, "seg": None, "files_n": 0, "files_rev": 0}),
        state_reply(rec={"live": True, "mode": "meeting", "seg": 3, "files_n": 2, "files_rev": 17, **HEALTH}),
        state_reply(rec={**REC_IDLE, **dict.fromkeys(HEALTH, 4294967295), "ring_high": 65535}),  # schema maxima
        state_reply(rec={**REC_IDLE, **dict.fromkeys(HEALTH)}),  # null: not measured by this build
    ],
    ids=lambda m: m["t"],
)
def test_message_is_accepted(contract: schemas.Contract, msg: dict) -> None:
    assert schemas.field_errors(contract, msg) == ()


# --- rejected -------------------------------------------------------------------

REJECTED = [
    ({"v": 0, "t": "file.list", "id": "h41", "max": 5}, "max"),
    ({"v": 0, "t": "file.list", "id": "h41", "max": 0}, "max"),
    ({"v": 0, "t": "file.list", "id": "h41", "after": "h40"}, "after"),
    ({"v": 0, "t": "file.list", "id": "h41", "re": "d1"}, "re"),
    (get_msg(len=1381), "len"),
    (get_msg(len=0), "len"),
    (get_msg(off=-1), "off"),
    (get_msg(off=4294967296), "off"),
    (get_msg(fid="3qzk1m7-00042-0a1c"), "fid"),
    ({"v": 0, "t": "file.get", "id": "h42", "fid": FID, "len": 1380}, "off"),
    (data_msg(b"abc", b64=None), "b64"),
    (data_msg(b"abc", b64="YWI="), "b64"),
    (data_msg(b"", b64="AAAA"), "b64"),
    (data_msg(b"abcd", b64="YWJjZA"), "b64"),
    (data_msg(b"abc", size=43), "size"),
    (data_msg(b"abc", size=19200045), "size"),
    (data_msg(b"abc", crc=4294967296), "crc"),
    ({k: v for k, v in data_msg(b"abc").items() if k != "re"}, "re"),
    (items_msg(*[item()] * 5), "items"),
    (items_msg(item(stars=list(range(17)))), "items[0].stars"),
    (items_msg(item(stars=[600000])), "items[0].stars[0]"),
    (items_msg(item(consent="yes")), "items[0].consent"),
    (items_msg(item(sha256=SHA.upper())), "items[0].sha256"),
    (items_msg(item(kind="note")), "items[0].kind"),
    (items_msg(item(seg=10000)), "items[0].seg"),
    (items_msg(item(mode="call")), "items[0].mode"),
    (items_msg(item(lead_ms=2001)), "items[0].lead_ms"),
    (items_msg(item(dur_ms=600001)), "items[0].dur_ms"),
    (items_msg(item(size=19200045)), "items[0].size"),
    (items_msg(item(at={"wall_ms": None})), "items[0].at.up_ms"),
    (items_msg(item(slot=10)), "items[0].slot"),
    ({k: v for k, v in items_msg().items() if k != "more"}, "more"),
    (del_msg(sha256=SHA.upper()), "sha256"),
    (del_msg(sha256=SHA[:-1]), "sha256"),
    (del_msg(fid="h99"), "fid"),
    ({k: v for k, v in del_msg().items() if k != "size"}, "size"),
    ({"v": 0, "t": "debug.rec", "id": "h80", "seg_ms": 9999}, "seg_ms"),
    ({"v": 0, "t": "debug.rec", "id": "h80", "seg_ms": 600001}, "seg_ms"),
    ({"v": 0, "t": "debug.rec", "id": "h80"}, "seg_ms"),
    (pong(files={"n": 257, "rev": 1, "live": False}), "files.n"),
    (pong(files={"n": 1, "rev": -1, "live": False}), "files.rev"),
    (pong(files={"n": 1, "rev": 1, "live": "no"}), "files.live"),
    (pong(files={"n": 1, "rev": 1}), "files.live"),
    (device_hello(files={"n": 1, "rev": 4294967296, "live": False}), "files.rev"),
    (state_reply(rec={"live": True, "mode": "call", "seg": 0, "files_n": 0, "files_rev": 0}), "rec.mode"),
    (state_reply(rec={"live": True, "mode": "memo", "seg": 0, "files_n": 257, "files_rev": 0}), "rec.files_n"),
    (state_reply(rec={"live": True, "mode": "memo", "seg": 0, "files_n": 1}), "rec.files_rev"),
    (state_reply(rec={**REC_IDLE, **HEALTH, "ring_high": 65536}), "rec.ring_high"),
    (state_reply(rec={**REC_IDLE, **HEALTH, "mic_missing": -1}), "rec.mic_missing"),
    (state_reply(rec={**REC_IDLE, **HEALTH, "rot_ms_max": 4294967296}), "rec.rot_ms_max"),
    (state_reply(rec={**REC_IDLE, **HEALTH, "heap_free": "170000"}), "rec.heap_free"),
    (state_reply(rec={**REC_IDLE, **HEALTH, "heap_block": 1.5}), "rec.heap_block"),
]


@pytest.mark.parametrize(("msg", "field"), REJECTED, ids=[f"{m['t']}:{f}" for m, f in REJECTED])
def test_message_is_rejected_on_one_field(contract: schemas.Contract, msg: dict, field: str) -> None:
    assert schemas.field_errors(contract, msg) == (field,)


def test_oversized_chunk_breaks_n_and_b64(contract: schemas.Contract) -> None:
    assert schemas.field_errors(contract, data_msg(b"\x00" * 1381)) == ("b64", "n")


def test_b64_length_check_ignores_non_strings(contract: schemas.Contract) -> None:
    assert schemas.field_errors(contract, data_msg(b"abc", b64=7)) == ("b64",)
    assert schemas.field_errors(contract, data_msg(b"abc", n="3")) == ("n",)
