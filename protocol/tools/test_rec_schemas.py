"""Tests for the v0.4.1 schemas (docs/13 §12.2): hello caps and host identity, dest, rec.list / rec.items / rec.act."""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas

PROTOCOL = Path(__file__).resolve().parents[1]
REC = "3QZK1M7-00009-0001"
CURSOR = "t1792440000000-a1"
HOST_ID = "7f3a9c1e5b2d4a60"
TITLE_48 = "客户同步定价和交付会议纪要一二"  # 16 CJK characters = 48 UTF-8 bytes


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def host_hello(caps: dict | None = None, **extra: object) -> dict:
    base = {"v": 0, "t": "hello", "id": "h1", "role": "host", "app": "cpad/0.4.1", "agent": "ok"}
    return {**base, "caps": {"spawn": True, "reply": False, **(caps or {})}, **extra}


def note(**body: object) -> dict:
    return {
        "v": 0, "t": "outbox.item", "id": "3QZK1M7-0000C-0005", "kind": "note",
        "at": {"up_ms": 98000, "wall_ms": 1792440045000}, "body": {"text": "call the plumber", **body},
    }  # fmt: skip


def file_items(**item: object) -> dict:
    entry = {
        "id": REC, "kind": "rec", "size": 384044, "rec": REC, "seg": 0, "last": True, "mode": "memo",
        "dur_ms": 12000, "at": {"up_ms": 435000, "wall_ms": None}, **item,
    }  # fmt: skip
    return {"v": 0, "t": "file.items", "id": "d41", "re": "h41", "items": [entry], "more": False}


def rec_list(**extra: object) -> dict:
    return {"v": 0, "t": "rec.list", "id": "d23", "filter": "all", "max": 6, **extra}


def row(**extra: object) -> dict:
    return {"rec": REC, "mode": "meeting", "st": "done", **extra}


FULL_ROW = row(
    wall_ms=1792440000000, dur_ms=2530000, project="cpa", starred=True, marks=2, title="客户同步：定价和交付", todo_n=3
)


def rec_items(*rows: dict, **extra: object) -> dict:
    return {"v": 0, "t": "rec.items", "id": "h24", "re": "d23", "items": list(rows), **extra}


def rec_act(op: str = "hide", **extra: object) -> dict:
    return {"v": 0, "t": "rec.act", "id": "d27", "rec": REC, "op": op, **extra}


def other_rows(n: int) -> list[dict]:
    return [row(rec=f"3QZK1M7-00009-000{i}") for i in range(1, n + 1)]


# --- limits ----------------------------------------------------------------------

NEW_LIMITS = {
    "rec_list_max": 6,
    "rec_title_max": 48,
    "rec_cursor_max": 32,
    "rec_cursor_pattern": "^[A-Za-z0-9_-]{1,32}$",
    "host_label_max": 24,
    "host_id_pattern": "^[a-z0-9-]{8,32}$",
    "dest_pattern": "^[a-z][a-z0-9-]{0,11}(\\.[a-z0-9-]{1,12}){0,2}$",
    "dest_max": 24,
}


def test_limits_json_has_every_v041_key(contract: schemas.Contract) -> None:
    assert {k: contract.limits.get(k) for k in NEW_LIMITS} == NEW_LIMITS


def test_every_v041_limit_is_tied_to_a_schema(contract: schemas.Contract) -> None:
    assert set(NEW_LIMITS) <= {key for key, _, _ in schemas.LIMIT_POINTERS}
    assert schemas.limit_mismatches(contract) == ()


def test_rec_state_lists_the_daemon_session_states(contract: schemas.Contract) -> None:
    states = contract.common["$defs"]["recState"]["enum"]
    assert states == [
        "on_device", "pulling", "archived", "processing", "blocked", "done", "transcript", "failed", "stale",
    ]  # fmt: skip


def test_no_new_error_codes(contract: schemas.Contract) -> None:
    assert len(contract.common["$defs"]["errorCode"]["enum"]) == 16


# --- accepted -------------------------------------------------------------------

ACCEPTED = [
    host_hello(),
    host_hello({"recs": True, "todo": True}, host_id=HOST_ID, host_label="Studio Mac"),
    host_hello({"recs": None, "todo": None}, host_id=None, host_label=None),
    host_hello({"recs": False}, host_id="a" * 32, host_label="八个汉字的主机名字"[:8]),
    note(dest="todo"),
    note(dest=None, project="cpa"),
    note(dest="ww.plugin.todoist"),
    note(dest="daily"),
    note(dest="z" * 12 + "." + "y" * 11),
    file_items(dest="todo"),
    file_items(dest=None),
    rec_list(),
    rec_list(filter="starred", max=1, cursor=CURSOR),
    rec_list(filter="memo", cursor=None),
    rec_list(filter="meeting", cursor="_" * 32),
    rec_items(),
    rec_items(FULL_ROW, next=CURSOR),
    rec_items(*other_rows(6), next=None),
    rec_items(row(wall_ms=None, dur_ms=None, project=None, starred=None, marks=None, title=None, todo_n=None)),
    rec_items(row(title=TITLE_48, st="on_device", mode="memo")),
    rec_act("star"),
    rec_act("unstar", project=None),
    rec_act("tag", project="cpa"),
    rec_act("tag", project=None),
    rec_act("hide"),
]


@pytest.mark.parametrize("msg", ACCEPTED, ids=lambda m: m["t"])
def test_message_is_accepted(contract: schemas.Contract, msg: dict) -> None:
    assert schemas.field_errors(contract, msg) == ()


# --- rejected -------------------------------------------------------------------

REJECTED = [
    (host_hello({"recs": "yes"}), "caps.recs"),
    (host_hello({"todo": 1}), "caps.todo"),
    (host_hello(host_id="7F3A9C1E5B2D4A60"), "host_id"),
    (host_hello(host_id="abc1234"), "host_id"),
    (host_hello(host_id="a" * 33), "host_id"),
    (host_hello(host_id="7f3a_9c1e"), "host_id"),
    (host_hello(host_label="a" * 25), "host_label"),
    (host_hello(host_label="九个汉字的主机名字呀"), "host_label"),
    (host_hello(host_label=""), "host_label"),
    (host_hello(host_label="tab\there"), "host_label"),
    (note(dest="Todo"), "body.dest"),
    (note(dest=""), "body.dest"),
    (note(dest="todo."), "body.dest"),
    (note(dest="1todo"), "body.dest"),
    (note(dest="a.b.c.d"), "body.dest"),
    (note(dest="z" * 13), "body.dest"),
    (note(dest="z" * 12 + "." + "y" * 12), "body.dest"),
    (note(dest=7), "body.dest"),
    (file_items(dest="Todo"), "items[0].dest"),
    (rec_list(max=7), "max"),
    (rec_list(max=0), "max"),
    (rec_list(max=None), "max"),
    ({k: v for k, v in rec_list().items() if k != "max"}, "max"),
    ({k: v for k, v in rec_list().items() if k != "filter"}, "filter"),
    (rec_list(filter="hidden"), "filter"),
    (rec_list(cursor="a" * 33), "cursor"),
    (rec_list(cursor="t1792440000000/a1"), "cursor"),
    (rec_list(cursor=""), "cursor"),
    (rec_list(re="h1"), "re"),
    (rec_items(*other_rows(7)), "items"),
    (rec_items(row(), row()), "items[1].rec"),
    (rec_items(next="a" * 33), "next"),
    (rec_items(next="t1/2"), "next"),
    (rec_items(row(title="一" * 16 + "a")), "items[0].title"),
    (rec_items(row(title="")), "items[0].title"),
    (rec_items(row(title="line\nbreak")), "items[0].title"),
    (rec_items(row(st="deleted")), "items[0].st"),
    (rec_items(row(mode="call")), "items[0].mode"),
    (rec_items(row(rec="3qzk1m7-00009-0001")), "items[0].rec"),
    (rec_items(row(project="CPA")), "items[0].project"),
    (rec_items(row(starred="yes")), "items[0].starred"),
    (rec_items(row(marks=65536)), "items[0].marks"),
    (rec_items(row(todo_n=-1)), "items[0].todo_n"),
    (rec_items(row(dur_ms=4294967296)), "items[0].dur_ms"),
    (rec_items(row(wall_ms=1000)), "items[0].wall_ms"),
    ({k: v for k, v in rec_items().items() if k != "items"}, "items"),
    ({k: v for k, v in rec_items().items() if k != "re"}, "re"),
    (rec_act("delete"), "op"),
    (rec_act("trash"), "op"),
    (rec_act("unhide"), "op"),
    (rec_act("tag", project="Not A Key"), "project"),
    (rec_act("tag", project="cpa/x"), "project"),
    (rec_act("tag"), "project"),
    (rec_act("star", project="cpa"), "project"),
    (rec_act("hide", project="cpa"), "project"),
    (rec_act(rec="h99"), "rec"),
    ({k: v for k, v in rec_act().items() if k != "rec"}, "rec"),
    ({k: v for k, v in rec_act().items() if k != "op"}, "op"),
]


@pytest.mark.parametrize(("msg", "field"), REJECTED, ids=[f"{m['t']}:{f}" for m, f in REJECTED])
def test_message_is_rejected_on_one_field(contract: schemas.Contract, msg: dict, field: str) -> None:
    assert schemas.field_errors(contract, msg) == (field,)


@pytest.mark.parametrize("field", ["rec", "mode", "st"])
def test_row_requires_rec_mode_and_state(contract: schemas.Contract, field: str) -> None:
    bare = {k: v for k, v in row().items() if k != field}
    assert schemas.field_errors(contract, rec_items(bare)) == (f"items[0].{field}",)


@pytest.mark.parametrize(("name", "direction"), [("rec.list", ["d2h"]), ("rec.items", ["h2d"]), ("rec.act", ["d2h"])])
def test_rec_types_travel_one_way(contract: schemas.Contract, name: str, direction: list[str]) -> None:
    assert contract.schemas[name]["x-dir"] == direction


def test_no_schema_closes_its_fields(contract: schemas.Contract) -> None:
    texts = [str(contract.common), *(str(s) for s in contract.schemas.values())]
    assert not any("additionalProperties" in text for text in texts)
