"""Every protocol vector decodes to the same verdict in cpad as in the reference (05 §6, protocol/README)."""

from __future__ import annotations

import json

import pytest

from cpad.protocol import codec
from cpad.protocol.limits import LIMITS
from tests.support.protocol_files import PROTOCOL, message_cases, raw_line

CASES = message_cases()


def _label(case: tuple[str, str, dict]) -> str:
    return f"{case[0]}:{case[1]}:{case[2]['name']}"


@pytest.mark.parametrize("case", CASES, ids=[_label(c) for c in CASES])
def test_vector_decodes_like_the_contract(case):
    _type, kind, vector = case
    decoded = codec.decode_line(raw_line(vector))
    if kind == "valid":
        assert decoded.outcome == "ok", decoded
        assert decoded.msg == vector["msg"]
    else:
        assert decoded.outcome == vector["code"], decoded
        if vector["code"] == "E_BAD_FIELD":
            assert decoded.fields == (vector["field"],)


VALID = [c for c in CASES if c[1] == "valid"]


@pytest.mark.parametrize("case", VALID, ids=[_label(c) for c in VALID])
def test_valid_vector_round_trips_through_encode(case):
    decoded = codec.decode_line(raw_line(case[2]))
    again = codec.decode_line(codec.encode(decoded.msg))
    assert again.outcome == "ok", again
    assert again.msg == decoded.msg


def test_every_message_type_has_a_validator():
    names = {p.name.removesuffix(".schema.json") for p in (PROTOCOL / "schema" / "v0").glob("*.schema.json")}
    assert names - {"common"} == set(codec.KNOWN_TYPES)


def test_limits_are_read_from_protocol_limits_json():
    on_disk = json.loads((PROTOCOL / "limits.json").read_text(encoding="utf-8"))
    assert dict(LIMITS) == on_disk
    assert LIMITS["line_max"] == 2048 and LIMITS["text_max"] == 500 and LIMITS["host_inflight_max"] == 4096


def test_reply_id_only_for_answerable_requests():
    ping = codec.decode_line(b'{"v":0,"t":"ping","id":"h1"}\n')
    assert ping.reply_id == "h1"
    pong = codec.decode_line(b'{"v":0,"t":"ack","id":"d1","re":"h1"}\n')
    assert pong.reply_id is None
    unknown_reply = codec.decode_line(b'{"v":0,"t":"foo.bar","id":"d2","re":"h1"}\n')
    assert unknown_reply.outcome == "ignored" and unknown_reply.reply_id is None
