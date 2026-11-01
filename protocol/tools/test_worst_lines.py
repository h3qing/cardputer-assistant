"""Worst-case line lengths of the v0.3 and v0.4.1 additions (docs/11 §5.5, docs/13 §12.2): every line fits line_max.

Each line uses a 24-character id and re and puts every number at its schema maximum: uint32 fields at
4294967295, wall_ms at the epochMs maximum (2^53 - 1), sizes at 44 + seg_ms_max * 32, booleans as false
(the longer literal), mode "meeting", every optional field present. The lines must also be schema-valid,
so a schema change that loosens a bound breaks the arithmetic here first.

The design note estimated 2024 / 1970 / 234 B by hand; this exact construction gives 2023 / 1988 / 238 B.
The bound that matters, <= line_max = 2048 B, holds for all three (smallest margin: 25 B for file.data).

v0.4.1: rec.items with 6 rows, 48-byte titles and a 32-byte next is 1580 B with CJK titles and 1868 B when every
title byte is a '"' (each one escapes to 2 bytes; no other character costs more on the wire). file.items items
gain dest: the only dest v0.4.1 firmware sends is "todo", which keeps a full page of 4 at 2044 B; a dest of more
than 5 B on all four items no longer fits (24 B: 2124 B), so the device lists fewer per page with more:true, as
file.items already allows (firmware file_fit_page); 3 items with 24-byte dests take 1621 B.
"""

from __future__ import annotations

import base64
from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
ID = "A" * 24
RE = "B" * 24
FID = "ZZZZZZZ-ZZZZZ-ZZZZ"
UINT32 = 4294967295
EPOCH_MAX = 9007199254740991
V041_DEST = "todo"  # Dest{Daily, Todo}: daily is omitted, so todo is the only dest v0.4.1 firmware sends
LONG_DEST = "a" * 12 + "." + "b" * 11  # 24 B, the destId maximum
CJK_TITLE = "录" * 16  # 48 UTF-8 bytes
QUOTE_TITLE = '"' * 48  # 48 bytes, 96 on the wire


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def size_max(contract: schemas.Contract) -> int:
    return 44 + contract.limits["seg_ms_max"] * 32


def worst_file_data(contract: schemas.Contract) -> dict:
    n = contract.limits["file_get_b64_max"]
    b64 = base64.b64encode(b"\xff" * n).decode()
    return {
        "v": 0, "t": "file.data", "id": ID, "re": RE, "fid": FID, "off": UINT32, "n": n,
        "size": size_max(contract), "crc": UINT32, "b64": b64,
    }  # fmt: skip


def worst_item(contract: schemas.Contract, dest: str | None = None) -> dict:
    limits = contract.limits
    seg_ms = limits["seg_ms_max"]
    item = {
        "id": FID, "kind": "rec", "size": size_max(contract), "sha256": "f" * 64, "rec": FID,
        "seg": limits["file_seg_max"], "last": False, "mode": "meeting", "dur_ms": seg_ms,
        "at": {"up_ms": UINT32, "wall_ms": EPOCH_MAX}, "project": "z" * 12, "slot": 9,
        "stars": [seg_ms - 1] * limits["file_marks_max"], "lead_ms": limits["lead_ms_max"],
        "consent": "ack", "rcv": False,
    }  # fmt: skip
    return {**item, "dest": dest} if dest else item


def worst_file_items(contract: schemas.Contract, dest: str | None = None, n: int | None = None) -> dict:
    entries = [worst_item(contract, dest)] * (n or contract.limits["file_list_max"])
    return {"v": 0, "t": "file.items", "id": ID, "re": RE, "items": entries, "more": False}


def worst_row(title: str) -> dict:
    return {
        "rec": FID, "mode": "meeting", "st": "processing", "wall_ms": EPOCH_MAX, "dur_ms": UINT32,
        "project": "z" * 12, "starred": False, "marks": 65535, "title": title, "todo_n": 65535,
    }  # fmt: skip


def worst_rec_items(contract: schemas.Contract, title: str = CJK_TITLE) -> dict:
    limits = contract.limits
    rows = [{**worst_row(title), "rec": f"ZZZZZZZ-ZZZZZ-ZZZ{i}"} for i in range(limits["rec_list_max"])]
    return {"v": 0, "t": "rec.items", "id": ID, "re": RE, "items": rows, "next": "_" * limits["rec_cursor_max"]}


def worst_pong(contract: schemas.Contract) -> dict:
    revs = {"projects": UINT32, "sessions": UINT32, "queue": UINT32}
    files = {"n": contract.limits["file_pending_max"], "rev": UINT32, "live": False}
    return {"v": 0, "t": "pong", "id": ID, "re": RE, "up_ms": UINT32, "pending": 65535, "revs": revs, "files": files}


@pytest.mark.parametrize(
    ("build", "expected"),
    [
        (worst_file_data, 2023),
        (worst_file_items, 1988),
        (worst_pong, 238),
        (lambda c: worst_file_items(c, V041_DEST), 2044),
        (lambda c: worst_file_items(c, LONG_DEST, 3), 1621),
        (worst_rec_items, 1580),
        (lambda c: worst_rec_items(c, QUOTE_TITLE), 1868),
    ],
    ids=["file.data", "file.items", "pong", "file.items+todo", "file.items+3x24B", "rec.items", "rec.items+quotes"],
)
def test_worst_case_line_fits(contract: schemas.Contract, build: object, expected: int) -> None:
    msg = build(contract)  # type: ignore[operator]
    line = wire.canonical_line(msg)
    assert wire.decode_line(contract, line).outcome == "ok"
    assert len(line) == expected
    assert len(line) <= contract.limits["line_max"]


def test_worst_items_use_every_optional_field(contract: schemas.Contract) -> None:
    optional = set(contract.common["$defs"]["fileItem"]["properties"]) - set(
        contract.common["$defs"]["fileItem"]["required"]
    )
    assert optional <= set(worst_item(contract, LONG_DEST))
    assert optional - {"dest"} <= set(worst_item(contract))


def test_worst_rows_use_every_field_at_its_maximum(contract: schemas.Contract) -> None:
    row = contract.common["$defs"]["recRow"]
    assert set(row["properties"]) <= set(worst_row(CJK_TITLE))
    assert len(CJK_TITLE.encode()) == len(QUOTE_TITLE) == contract.limits["rec_title_max"]


def test_a_long_dest_on_every_item_needs_a_shorter_page(contract: schemas.Contract) -> None:
    line = wire.canonical_line(worst_file_items(contract, LONG_DEST))
    assert len(line) == 2124 > contract.limits["line_max"]
