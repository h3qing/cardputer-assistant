"""Codec rules not covered by the vectors: encoding, size fitting, odd inputs (05 §6.1, §6.2)."""

from __future__ import annotations

import json

import pytest
from hypothesis import given
from hypothesis import strategies as st

from cpad.protocol import codec


def test_encode_is_compact_utf8_without_ascii_escapes():
    line = codec.encode({"v": 0, "t": "ping", "id": "h1", "name": "博客"})
    assert line == '{"v":0,"t":"ping","id":"h1","name":"博客"}\n'.encode()


def test_encode_rejects_lines_over_line_max():
    with pytest.raises(codec.LineTooLarge):
        codec.encode({"v": 0, "t": "ping", "id": "h1", "pad": "x" * 2048})


def test_encode_accepts_exactly_line_max_bytes():
    base = codec.encode({"v": 0, "t": "ping", "id": "h1", "pad": ""})
    msg = {"v": 0, "t": "ping", "id": "h1", "pad": "x" * (2048 - len(base))}
    assert len(codec.encode(msg)) == 2048


def test_fit_list_drops_entries_from_the_tail_until_the_line_fits():
    rows = [{"name": "会" * 10, "i": i} for i in range(200)]
    msg, kept = codec.fit_list({"v": 0, "t": "sessions.snapshot", "id": "h1", "rows": rows}, "rows")
    assert len(codec.encode(msg)) <= 2048
    assert kept == len(msg["rows"]) and 0 < kept < 200
    assert msg["rows"] == rows[:kept]
    assert len(codec.serialize({**msg, "rows": rows[: kept + 1]})) > 2048


def test_fit_list_keeps_short_lists_unchanged():
    msg = {"v": 0, "t": "queue.snapshot", "id": "h1", "items": [1, 2]}
    assert codec.fit_list(msg, "items") == (msg, 2)


def test_carriage_returns_are_ignored_and_do_not_count():
    line = b'{"v":0,"t":"ping",\r"id":"h1"}' + b"\r" * 3000 + b"\n"
    assert codec.decode_line(line).outcome == "ok"


def test_noise_and_empty_lines():
    assert codec.decode_line(b"\n").outcome == "noise"
    assert codec.decode_line(b"ESP-ROM:esp32s3\n").outcome == "noise"


def test_json_that_is_not_an_object_is_a_parse_error():
    assert codec.decode_line(b"{1}\n").outcome == "E_PARSE"
    assert codec.decode_line(b'{"v":0}{"v":0}\n').outcome == "E_PARSE"


def test_float_version_is_a_bad_field():
    assert codec.decode_line(b'{"v":0.0,"t":"ping","id":"h1"}\n').fields == ("v",)


def test_unknown_type_without_re_is_answered():
    decoded = codec.decode_line(b'{"v":0,"t":"foo.bar","id":"h9"}\n')
    assert (decoded.outcome, decoded.reply_id) == ("E_UNKNOWN_TYPE", "h9")


def test_bad_id_is_not_answered():
    decoded = codec.decode_line(b'{"v":0,"t":"ping","id":"bad id"}\n')
    assert (decoded.outcome, decoded.reply_id) == ("E_BAD_FIELD", None)


def test_surrogate_escape_is_invalid_utf8():
    decoded = codec.decode_line(b'{"v":0,"t":"queue.done","id":"d1","qid":"\\ud800"}\n')
    assert decoded.fields == ("qid",)


def test_invalid_utf8_in_a_key_is_reported():
    decoded = codec.decode_line(b'{"v":0,"t":"ping","id":"h1","\xff":1}\n')
    assert decoded.outcome == "E_BAD_FIELD"


@given(st.dictionaries(st.text(max_size=8), st.text(max_size=40), max_size=6))
def test_round_trip_of_unknown_fields_on_a_ping(extra):
    msg = {**extra, "v": 0, "t": "ping", "id": "h1"}
    line = json.dumps(msg, ensure_ascii=False, separators=(",", ":")).encode("utf-8", "surrogatepass") + b"\n"
    decoded = codec.decode_line(line)
    assert decoded.outcome in {"ok", "E_BAD_FIELD", "E_TOO_LARGE"}
    if decoded.outcome == "ok":
        assert decoded.msg == msg


@given(st.binary(max_size=300))
def test_decoder_never_raises(data):
    assert codec.decode_line(data).outcome
