"""Worst-case line lengths of the v0.5 destination messages (docs/14-destinations.md §7.6).

Built like test_worst_lines.py: a 24-character message id, every number at its schema maximum (rev 4294967295,
slots 1..12), every optional field present with its longest schema-valid literal (null for key: 4 bytes
against 3 for "z"; fl 8 letters), every string at its limit (dest id 20 characters since names are at most 12,
docs/25 §2.2; label 24 UTF-8 bytes, kind 7 characters). The worst line must be schema-valid, so loosening a
bound in the schema breaks the arithmetic here first.

dests.snapshot: 12 rows -> 1507 B (margin 541 B to line_max; docs/14's three booleans and 16-character names
needed 1843 B). The same rows with 24 '"' as labels would need 1795 B, still under line_max but only because fl
is short; labels may not contain '"' or '\\' (each would be escaped to 2 bytes) so that no row ever needs escaping.
"""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
ID = "A" * 24
UINT32 = 4294967295
DESTS_MAX = 12
WORST_DEST_ID = "abcdefg." + "z" * 12
WORST_LABEL = "一二三四五六七八"  # 8 CJK characters = 24 UTF-8 bytes


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def worst_row(slot: int, label: str = WORST_LABEL) -> dict:
    return {
        "id": WORST_DEST_ID, "slot": slot, "label": label, "kind": "abcdefg", "key": None, "fl": "abcdefgh",
    }  # fmt: skip


def worst_dests_snapshot(label: str = WORST_LABEL) -> dict:
    rows = [worst_row(slot, label) for slot in range(1, DESTS_MAX + 1)]
    return {"v": 0, "t": "dests.snapshot", "id": ID, "rev": UINT32, "dests": rows}


def test_worst_dests_snapshot_fits(contract: schemas.Contract) -> None:
    line = wire.canonical_line(worst_dests_snapshot())
    assert wire.decode_line(contract, line).outcome == "ok"
    assert len(line) == 1507
    assert len(line) <= contract.limits["line_max"]


def test_worst_row_uses_every_field(contract: schemas.Contract) -> None:
    fields = contract.schemas["dests.snapshot"]["properties"]["dests"]["items"]["properties"]
    assert set(fields) == set(worst_row(1))


def test_worst_row_uses_the_longest_literal_of_each_optional_field() -> None:
    def size(value: object) -> int:
        return len(wire.canonical_line({"x": value}))

    assert size(None) > size("z") and size("abcdefgh") == max(size("a" * n) for n in range(9))


def test_worst_row_is_at_every_limit(contract: schemas.Contract) -> None:
    defs = contract.schemas["dests.snapshot"]["$defs"]
    assert len(WORST_DEST_ID) == 20
    assert len(WORST_LABEL.encode("utf-8")) == defs["label"]["x-maxBytes"]
    assert contract.schemas["dests.snapshot"]["properties"]["dests"]["maxItems"] == DESTS_MAX


def test_quotes_in_labels_are_refused_although_they_would_now_fit(contract: schemas.Contract) -> None:
    line = wire.canonical_line(worst_dests_snapshot(label='"' * 24))
    assert len(line) == 1795 <= contract.limits["line_max"]
    assert wire.decode_line(contract, line).outcome == "E_BAD_FIELD"
