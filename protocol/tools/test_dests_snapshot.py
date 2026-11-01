"""Tests for the v0.5 dests.snapshot schema and its vectors (docs/14-destinations.md §7.2, §7.6, §10.4).

The host pushes the capture destinations the device may offer; the device caches them and binds letters
from them while offline. Rows carry no secrets and need no escaping: labels exclude '"', '\\' and control
characters, so 12 rows at every limit stay under line_max (test_dest_worst_lines.py).
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]
VECTORS = PROTOCOL / "vectors" / "v0" / "messages" / "dests.snapshot.json"
TYPE = "dests.snapshot"
DESTS_MAX = 12  # docs/14 §4.2 / dest.model.DESTS_MAX
SLOT_MAX = 12  # dest.specs.SLOT_MAX
LABEL_MAX = 24  # dest.model.LABEL_MAX, UTF-8 bytes


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def row(**extra: object) -> dict:
    return {"id": "clip", "slot": 5, "label": "剪贴板", "kind": "clip", **extra}


def snap(*rows: dict, **extra: object) -> dict:
    return {"v": 0, "t": TYPE, "id": "h12", "rev": 2918374651, "dests": list(rows), **extra}


def numbered(count: int) -> list[dict]:
    return [row(id=f"file.n{i}", slot=i, label=f"n{i}", kind="file") for i in range(1, count + 1)]


# --- shape -----------------------------------------------------------------------


def test_schema_is_a_host_request(contract: schemas.Contract) -> None:
    schema = contract.schemas[TYPE]
    assert schema["x-dir"] == ["h2d"]
    assert schema["allOf"] == [{"$ref": "common.schema.json#/$defs/request"}]
    assert set(schema["required"]) == {"rev", "dests"}


def test_bounds_match_the_design_constants(contract: schemas.Contract) -> None:
    dests = contract.schemas[TYPE]["properties"]["dests"]
    fields = dests["items"]["properties"]
    assert dests["maxItems"] == DESTS_MAX
    assert dests["x-uniqueBy"] == "slot"
    assert fields["slot"]["maximum"] == SLOT_MAX
    assert contract.schemas[TYPE]["$defs"]["label"]["x-maxBytes"] == LABEL_MAX
    assert set(dests["items"]["required"]) == {"id", "slot", "label", "kind"}


def test_dest_id_pattern_is_the_design_pattern(contract: schemas.Contract) -> None:
    assert contract.schemas[TYPE]["$defs"]["destId"]["pattern"] == r"^[a-z]{3,7}(\.[a-z0-9][a-z0-9-]{0,11})?$"


def test_every_dest_id_is_also_a_v041_dest(contract: schemas.Contract) -> None:
    """docs/25 §2.2: names of at most 12 characters keep every row id inside limits.json dest_pattern."""
    import re

    wide = re.compile(contract.limits["dest_pattern"])
    for dest_id in ("abcdefg." + "z" * 12, "url.grok", "file.0-a", "daily"):
        assert wide.fullmatch(dest_id), dest_id


# --- accepted -------------------------------------------------------------------

ACCEPTED = [
    snap(),
    snap(row()),
    snap(row(id="daily", slot=1, label="今日笔记", kind="daily", key="n")),
    snap(row(key="c", fl="l")),
    snap(row(id="queue", slot=3, label="项目队列", kind="queue", key="q", fl="p")),
    snap(row(id="file.sb", slot=7, label="reading-list", kind="file", key="o", fl="c")),
    snap(row(id="url.grok", slot=8, label="Grok", kind="url", key="x", fl="lc")),
    snap(row(id="url.cursor", slot=9, label="Cursor", kind="url", key="u", fl="lck")),
    snap(row(id="app.notion", slot=10, label="Notion", kind="app", key="b", fl="l")),
    snap(row(id="paste.ed", slot=11, label="编辑器", kind="paste", fl="l")),
    snap(row(key=None)),
    snap(row(fl="")),
    snap(row(fl=None)),  # null is absent: the device reads lck
    snap(row(fl="abcdefgh")),
    snap(row(id="paste", slot=6, label="光标处", kind="paste", fl="lk")),
    snap(row(id="fax", slot=9, label="fax", kind="fax", fl="lcz")),  # a future kind and flag: shown, fl decides
    snap(row(key="z")),  # a letter the device does not bind: shown, not bound (docs/14 §3.1)
    snap(row(key="c"), row(id="file.x", slot=7, label="x", kind="file", key="c")),  # first one wins (§10.4)
    snap(row(id="abcdefg." + "z" * 12, slot=SLOT_MAX, label="一二三四五六七八", kind="abcdefg")),
    snap(row(icon="📋", default=True)),  # unknown fields are ignored (protocol/README, versioning rules)
    snap(*numbered(DESTS_MAX)),
    snap(row(), rev=0),
    snap(row(), rev=4294967295),
]


@pytest.mark.parametrize("msg", ACCEPTED, ids=range(len(ACCEPTED)))
def test_snapshot_is_accepted(contract: schemas.Contract, msg: dict) -> None:
    assert schemas.field_errors(contract, msg) == ()


# --- rejected -------------------------------------------------------------------

REJECTED = [
    ({k: v for k, v in snap(row()).items() if k != "rev"}, "rev"),
    ({k: v for k, v in snap(row()).items() if k != "dests"}, "dests"),
    (snap(row(), rev=-1), "rev"),
    (snap(row(), rev=4294967296), "rev"),
    (snap(row(), re="d1"), "re"),
    (snap(row(), dests={}), "dests"),
    (snap(row(slot=1), row(id="todo", slot=1)), "dests[1].slot"),
    (snap(row(slot=0)), "dests[0].slot"),
    (snap(row(slot=SLOT_MAX + 1)), "dests[0].slot"),
    (snap(row(slot="5")), "dests[0].slot"),
    (snap(row(id="Clip")), "dests[0].id"),
    (snap(row(id="cl")), "dests[0].id"),
    (snap(row(id="clipboard")), "dests[0].id"),
    (snap(row(id="file.")), "dests[0].id"),
    (snap(row(id="file.-sb")), "dests[0].id"),
    (snap(row(id="file.a.b")), "dests[0].id"),
    (snap(row(id="file." + "z" * 13)), "dests[0].id"),
    (snap(row(id="file.sb\n")), "dests[0].id"),
    (snap(row(label="")), "dests[0].label"),
    (snap(row(label="一二三四五六七八九")), "dests[0].label"),
    (snap(row(label='say "hi"')), "dests[0].label"),
    (snap(row(label="C:\\notes")), "dests[0].label"),
    (snap(row(label="two\nlines")), "dests[0].label"),
    (snap(row(label="del\u007f")), "dests[0].label"),
    (snap(row(label=None)), "dests[0].label"),
    (snap(row(kind="Clip")), "dests[0].kind"),
    (snap(row(kind="file.sb")), "dests[0].kind"),
    (snap(row(kind="ab")), "dests[0].kind"),
    (snap(row(kind="abcdefgh")), "dests[0].kind"),
    (snap(row(key="C")), "dests[0].key"),
    (snap(row(key="cc")), "dests[0].key"),
    (snap(row(key="1")), "dests[0].key"),
    (snap(row(key="")), "dests[0].key"),
    (snap(row(fl=True)), "dests[0].fl"),
    (snap(row(fl="LC")), "dests[0].fl"),
    (snap(row(fl="lcpkabcde")), "dests[0].fl"),
    (snap(row(fl="l c")), "dests[0].fl"),
    (snap({k: v for k, v in row().items() if k != "kind"}), "dests[0].kind"),
    (snap("clip"), "dests[0]"),
]


@pytest.mark.parametrize(("msg", "field"), REJECTED, ids=[f for _, f in REJECTED])
def test_snapshot_is_rejected_on_one_field(contract: schemas.Contract, msg: dict, field: str) -> None:
    assert schemas.field_errors(contract, msg) == (field,)


def test_thirteen_rows_are_rejected(contract: schemas.Contract) -> None:
    # 13 rows cannot all have distinct slots in 1..12, so a slot fault comes along: no single-field vector.
    assert "dests" in schemas.field_errors(contract, snap(*numbered(DESTS_MAX + 1)))


# --- vectors ---------------------------------------------------------------------


# (field, predicate on the invalid msg): cases the firmware codec (F5-0) must reject the same way.
def _first(msg: dict, key: str) -> object:
    return msg["dests"][0].get(key)


def _label(msg: dict) -> str:
    return str(_first(msg, "label") or "")


REQUIRED_INVALID = (
    ("dests[1].slot", lambda m: len({d["slot"] for d in m["dests"]}) < len(m["dests"])),
    ("dests[0].slot", lambda m: _first(m, "slot") == SLOT_MAX + 1),
    ("dests[0].label", lambda m: '"' in _label(m)),
    ("dests[0].label", lambda m: "\\" in _label(m)),
    ("dests[0].label", lambda m: len(_label(m).encode()) == LABEL_MAX + 3),
    ("dests[0].id", lambda m: str(_first(m, "id")).count(".") > 1),
    ("dests[0].kind", lambda m: "." in str(_first(m, "kind"))),
    ("dests[0].key", lambda m: _first(m, "key") == "C"),
    ("dests[0].id", lambda m: len(str(_first(m, "id")).partition(".")[2]) == 13),
    ("dests[0].fl", lambda m: _first(m, "fl") is True),
    ("dests[0].fl", lambda m: len(str(_first(m, "fl"))) == 9),
    ("re", lambda m: "re" in m),
)


def _vectors() -> dict:
    return json.loads(VECTORS.read_text(encoding="utf-8"))


@pytest.mark.parametrize(
    ("field", "wanted"), REQUIRED_INVALID, ids=[f"{f}#{i}" for i, (f, _) in enumerate(REQUIRED_INVALID)]
)
def test_vectors_carry_the_required_invalid_case(field: str, wanted: object) -> None:
    cases = [c for c in _vectors()["invalid"] if c.get("field") == field and "msg" in c]
    assert any(wanted(c["msg"]) for c in cases)  # type: ignore[operator]


def test_valid_vectors_cover_the_design_example_and_the_worst_case(contract: schemas.Contract) -> None:
    sizes = sorted(len(wire.canonical_line(c["msg"])) for c in _vectors()["valid"])
    assert 542 in sizes  # docs/14 §7.2 example, six rows, with fl (docs/25 §2.2)
    assert sizes[-1] == 1507  # docs/25 §4.6 worst case, twelve rows (key null, fl 8 letters)


def test_every_valid_vector_has_rows_sorted_by_slot() -> None:
    for case in _vectors()["valid"]:
        slots = [d["slot"] for d in case["msg"]["dests"]]
        assert slots == sorted(slots), case["name"]
