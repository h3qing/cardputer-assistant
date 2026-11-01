"""Tests for the reference line decoder (wire.py)."""

from __future__ import annotations

from pathlib import Path

import pytest
import schemas
import wire

PROTOCOL = Path(__file__).resolve().parents[1]


@pytest.fixture(scope="module")
def contract() -> schemas.Contract:
    return schemas.load_contract(PROTOCOL)


def _line(text: str) -> bytes:
    return text.encode("utf-8") + b"\n"


def test_canonical_line_is_compact_utf8_with_newline() -> None:
    msg = {"v": 0, "t": "ping", "id": "h1", "x": "中"}
    assert wire.canonical_line(msg) == '{"v":0,"t":"ping","id":"h1","x":"中"}\n'.encode()


def test_valid_host_initiated_message_expects_reply(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"ping","id":"h11"}'))
    assert got.outcome == "ok"
    assert got.msg == {"v": 0, "t": "ping", "id": "h11"}
    assert got.reply_id == "h11"


def test_valid_reply_expects_no_reply(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"ack","id":"d2","re":"h2"}'))
    assert got.outcome == "ok"
    assert got.reply_id is None


def test_carriage_return_is_ignored(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, b'{"v":0,"t":"ping","id":"h1"}\r\n')
    assert got.outcome == "ok"


def test_line_not_starting_with_brace_is_noise(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, b"ESP-ROM:esp32s3-20210327\n")
    assert got.outcome == "noise"
    assert got.reply_id is None


def test_line_of_exactly_line_max_is_accepted(contract: schemas.Contract) -> None:
    head = '{"v":0,"t":"ping","id":"h1"'
    pad = " " * (2048 - len(head) - 2)
    raw = _line(head + pad + "}")
    assert len(raw) == 2048
    assert wire.decode_line(contract, raw).outcome == "ok"


def test_line_over_line_max_is_too_large(contract: schemas.Contract) -> None:
    head = '{"v":0,"t":"ping","id":"h1"'
    raw = _line(head + " " * (2048 - len(head) - 1) + "}")
    assert len(raw) == 2049
    got = wire.decode_line(contract, raw)
    assert got.outcome == "E_TOO_LARGE"
    assert got.reply_id is None


@pytest.mark.parametrize(
    "text",
    ['{"v":0,"t":"ping","id":"h1"', '{"v":0,"t":"ping","id":"h1","x":NaN}', "{}x"],
)
def test_unparseable_json_is_e_parse(contract: schemas.Contract, text: str) -> None:
    got = wire.decode_line(contract, _line(text))
    assert got.outcome == "E_PARSE"
    assert got.reply_id is None


def test_other_major_version_is_e_version(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":1,"t":"ping","id":"h1"}'))
    assert got.outcome == "E_VERSION"
    assert got.reply_id == "h1"


@pytest.mark.parametrize(
    ("text", "field"),
    [
        ('{"t":"ping","id":"h1"}', "v"),
        ('{"v":true,"t":"ping","id":"h1"}', "v"),
        ('{"v":0,"t":"Ping","id":"h1"}', "t"),
        ('{"v":0,"t":"ping","id":"h_1"}', "id"),
        ('{"v":0,"t":"ping"}', "id"),
    ],
)
def test_bad_envelope_is_e_bad_field(contract: schemas.Contract, text: str, field: str) -> None:
    got = wire.decode_line(contract, _line(text))
    assert got.outcome == "E_BAD_FIELD"
    assert got.fields == (field,)


def test_bad_id_cannot_be_replied_to(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"ping","id":"x y"}'))
    assert got.reply_id is None


def test_unknown_type_without_re_is_e_unknown_type(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"foo.bar","id":"h9"}'))
    assert got.outcome == "E_UNKNOWN_TYPE"
    assert got.reply_id == "h9"


def test_unknown_type_with_re_is_ignored(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"foo.bar","id":"h9","re":"d1"}'))
    assert got.outcome == "ignored"
    assert got.reply_id is None


def test_invalid_utf8_is_e_bad_field_with_path(contract: schemas.Contract) -> None:
    raw = (
        b'{"v":0,"t":"outbox.item","id":"3QZK1M7-0000C-0003","kind":"note",'
        b'"at":{"up_ms":1,"wall_ms":null},"body":{"text":"bad \xc3\x28 byte"}}\n'
    )
    got = wire.decode_line(contract, raw)
    assert got.outcome == "E_BAD_FIELD"
    assert got.fields == ("body.text",)


def test_schema_violation_reports_field_and_reply_id(contract: schemas.Contract) -> None:
    text = '{"v":0,"t":"time.set","id":"h2","epoch_ms":1792440000000,"tz_min":900}'
    got = wire.decode_line(contract, _line(text))
    assert got.outcome == "E_BAD_FIELD"
    assert got.fields == ("tz_min",)
    assert got.reply_id == "h2"


def test_broken_reply_is_not_replied_to(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"ack","id":"d2","re":"h 2"}'))
    assert got.outcome == "E_BAD_FIELD"
    assert got.fields == ("re",)
    assert got.reply_id is None


def test_unknown_fields_are_ignored(contract: schemas.Contract) -> None:
    got = wire.decode_line(contract, _line('{"v":0,"t":"ping","id":"h1","x_future":[1,2]}'))
    assert got.outcome == "ok"
