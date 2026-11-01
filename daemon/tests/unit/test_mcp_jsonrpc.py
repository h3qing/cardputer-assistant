"""cpa mcp JSON-RPC 2.0 framing (docs/15 §3.2, §3.3): strict parse, fixed error codes, one line per message."""

from __future__ import annotations

import json

import pytest

from cpad.mcp import jsonrpc
from cpad.mcp.jsonrpc import Notification, Request


def _line(doc) -> bytes:
    return json.dumps(doc).encode()


def test_request_with_object_params():
    msg = jsonrpc.parse(_line({"jsonrpc": "2.0", "id": 7, "method": "tools/list", "params": {"a": 1}}))
    assert msg == Request(7, "tools/list", {"a": 1})


def test_string_id_and_missing_params():
    msg = jsonrpc.parse(_line({"jsonrpc": "2.0", "id": "x-1", "method": "ping"}))
    assert msg == Request("x-1", "ping", {})


def test_notification_has_no_id_key():
    msg = jsonrpc.parse(_line({"jsonrpc": "2.0", "method": "notifications/initialized"}))
    assert msg == Notification("notifications/initialized", {})


@pytest.mark.parametrize("bad_id", [None, True, False, 1.5, [], {}])
def test_id_must_be_string_or_integer(bad_id):
    reply = jsonrpc.parse(_line({"jsonrpc": "2.0", "id": bad_id, "method": "ping"}))
    assert reply["error"]["code"] == jsonrpc.INVALID_REQUEST
    assert reply["id"] is None


def test_batch_is_refused():
    reply = jsonrpc.parse(_line([{"jsonrpc": "2.0", "id": 1, "method": "ping"}]))
    assert reply == jsonrpc.error(None, jsonrpc.INVALID_REQUEST, "Batch requests are not supported")


@pytest.mark.parametrize(
    "raw",
    [b"{not json", b"\xff\xfe{}", b"[" * 200_000 + b"]" * 200_000, b"", b"   "],
)
def test_unparseable_lines_are_parse_errors_with_null_id(raw):
    reply = jsonrpc.parse(raw)
    assert reply["error"]["code"] == jsonrpc.PARSE_ERROR
    assert reply["id"] is None


@pytest.mark.parametrize(
    "doc",
    [
        "just a string",
        42,
        {"id": 1, "method": "ping"},
        {"jsonrpc": "1.0", "id": 1, "method": "ping"},
        {"jsonrpc": "2.0", "id": 1},
        {"jsonrpc": "2.0", "id": 1, "method": 5},
        {"jsonrpc": "2.0", "id": 1, "method": ""},
    ],
)
def test_malformed_requests_are_invalid_request(doc):
    reply = jsonrpc.parse(_line(doc))
    assert reply["error"]["code"] == jsonrpc.INVALID_REQUEST


def test_invalid_request_keeps_a_valid_id():
    reply = jsonrpc.parse(_line({"jsonrpc": "2.0", "id": 3, "method": 5}))
    assert reply["id"] == 3


@pytest.mark.parametrize("params", [[1, 2], "x", 3, None])
def test_non_object_params_are_invalid_params(params):
    reply = jsonrpc.parse(_line({"jsonrpc": "2.0", "id": 4, "method": "tools/list", "params": params}))
    assert reply == jsonrpc.error(4, jsonrpc.INVALID_PARAMS, "params must be an object")


def test_notification_with_bad_params_is_dropped_silently():
    assert jsonrpc.parse(_line({"jsonrpc": "2.0", "method": "notifications/cancelled", "params": [1]})) is None


def test_responses_from_the_client_are_ignored():
    assert jsonrpc.parse(_line({"jsonrpc": "2.0", "id": 1, "result": {}})) is None
    assert jsonrpc.parse(_line({"jsonrpc": "2.0", "id": 1, "error": {"code": 1, "message": "x"}})) is None


def test_oversized_line_is_refused_before_decoding():
    reply = jsonrpc.parse(b"{" + b" " * jsonrpc.LINE_MAX + b"}")
    assert reply == jsonrpc.error(None, jsonrpc.INVALID_REQUEST, "Message too large")


def test_encode_is_one_line_with_raw_utf8():
    data = jsonrpc.encode(jsonrpc.result(1, {"text": "第一行\n第二行"}))
    assert data.endswith(b"\n") and data.count(b"\n") == 1
    assert "第一行".encode() in data
    assert json.loads(data) == {"jsonrpc": "2.0", "id": 1, "result": {"text": "第一行\n第二行"}}


def test_encode_survives_lone_surrogates_from_the_client():
    msg = jsonrpc.parse(b'{"jsonrpc":"2.0","id":"\\ud800","method":"nope"}')
    data = jsonrpc.encode(jsonrpc.error(msg.id, jsonrpc.METHOD_NOT_FOUND, "Method not found"))
    assert json.loads(data.decode("ascii"))["id"] == "\ud800"


def test_error_omits_data_unless_given():
    assert "data" not in jsonrpc.error(1, jsonrpc.INTERNAL_ERROR, "Internal error")["error"]
    assert jsonrpc.error(1, -32022, "x", {"a": 1})["error"]["data"] == {"a": 1}
