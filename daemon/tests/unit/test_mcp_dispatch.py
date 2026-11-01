"""The request state machine (docs/15 §3.3, §3.6, §3.8): both eras, permissions, rate limits, envelopes."""

from __future__ import annotations

import json
from dataclasses import replace

import pytest

from cpad.mcp import catalog, dispatch
from cpad.mcp.audit import AuditError
from cpad.mcp.config import McpConfig
from cpad.mcp.dispatch import Env, State
from cpad.mcp.era import ClientInfo, Session
from cpad.mcp.jsonrpc import Request
from cpad.mcp.tools import Ctx
from tests.support.fake_reader import FakeReader, FakeWriter, fake_status

MODERN_META = {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
               "io.modelcontextprotocol/clientCapabilities": {},
               "io.modelcontextprotocol/clientInfo": {"name": "claude-code", "version": "2.5"}}  # fmt: skip
LEGACY = State(session=Session("2025-06-18", ClientInfo("codex", "0.98")))


def env(writes=frozenset(), disabled=frozenset()):
    ctx = Ctx(reader=FakeReader(), status=fake_status, writer=FakeWriter())
    return Env(ctx=ctx, tools=catalog.visible(McpConfig(writes, disabled)))


def run(state, method, params=None, e=None, now=0.0, msg_id=1):
    return dispatch.handle(state, Request(msg_id, method, params if params is not None else {}), e or env(), now)


def modern(params=None):
    return {**(params or {}), "_meta": MODERN_META}


def test_initialize_then_list_tools_in_the_legacy_era():
    state, reply, call = run(State(), "initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                                     "clientInfo": {"name": "codex", "version": "0.98"}})  # fmt: skip
    body = reply["result"]
    assert body["protocolVersion"] == "2025-06-18" and body["serverInfo"]["name"] == "cpa"
    assert body["capabilities"] == dispatch.CAPABILITIES and body["instructions"] == catalog.INSTRUCTIONS
    assert call.client == ClientInfo("codex", "0.98") and call.era == "legacy"
    state, reply, call = run(state, "tools/list")
    assert [t["name"] for t in reply["result"]["tools"]] == list(catalog.READ_NAMES)
    assert reply["result"]["cacheScope"] == "private" and reply["result"]["resultType"] == "complete"
    assert call.client.name == "codex"


def test_second_initialize_is_refused():
    _, reply, _ = run(LEGACY, "initialize", {"protocolVersion": "2025-11-25"})
    assert reply["error"]["code"] == -32600


def test_modern_request_needs_no_handshake():
    state, reply, call = run(State(), "tools/list", modern())
    assert len(reply["result"]["tools"]) == 12
    assert call.era == "modern" and call.client.name == "claude-code"
    assert state.session == Session()


def test_no_handshake_no_meta():
    _, reply, call = run(State(), "tools/list")
    assert reply["error"]["code"] == -32602 and call.error == -32602


def test_unsupported_modern_version():
    meta = {**MODERN_META, "io.modelcontextprotocol/protocolVersion": "2099-01-01"}
    _, reply, _ = run(State(), "tools/list", {"_meta": meta})
    assert reply["error"] == {"code": -32022, "message": "Unsupported protocol version",
                              "data": {"supported": ["2026-07-28"], "requested": "2099-01-01"}}  # fmt: skip


def test_discover_is_modern_only():
    _, reply, call = run(State(), "server/discover", modern())
    assert reply["result"]["supportedVersions"] == ["2026-07-28"] and reply["result"]["ttlMs"] > 0
    assert call.method == "server/discover"
    _, reply, _ = run(LEGACY, "server/discover")
    assert reply["error"]["code"] == -32601


def test_ping_per_era():
    assert run(LEGACY, "ping")[1] == {"jsonrpc": "2.0", "id": 1, "result": {}}
    assert run(State(), "ping")[1]["result"] == {}
    _, reply, call = run(State(), "ping", modern())
    assert reply["error"]["code"] == -32601 and call is None


@pytest.mark.parametrize("method", ["prompts/list", "resources/subscribe", "logging/setLevel", "completion/complete",
                                    "sampling/createMessage", "nope"])  # fmt: skip
def test_unknown_methods(method):
    assert run(LEGACY, method)[1]["error"]["code"] == -32601


def test_tools_list_rejects_cursors():
    assert run(LEGACY, "tools/list", {"cursor": "abc"})[1]["error"]["code"] == -32602


def test_tool_call_result_envelope():
    _, reply, call = run(LEGACY, "tools/call", {"name": "list_days", "arguments": {"limit": 1}})
    result = reply["result"]
    assert result["isError"] is False and result["resultType"] == "complete"
    assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
    assert result["structuredContent"]["trust"] == "untrusted-user-data"
    assert (call.tool, call.args, call.n) == ("list_days", ("limit",), 1)


def test_tool_error_is_a_result_not_a_protocol_error():
    _, reply, call = run(LEGACY, "tools/call", {"name": "get_recording", "arguments": {"id": "../x"}})
    result = reply["result"]
    assert result["isError"] is True and "structuredContent" not in result
    assert json.loads(result["content"][0]["text"])["error"]["code"] == "E_BAD_ARG"
    assert call.error == "E_BAD_ARG"


@pytest.mark.parametrize(
    "params",
    [{"name": "nope"}, {"name": 5}, {}, {"name": "queue_add", "arguments": {"project": "cpa", "text": "x"}},
     {"name": "list_days", "arguments": [1]}, {"name": "list_days", "arguments": "x"}],
)  # fmt: skip
def test_unknown_tools_and_bad_arguments_are_invalid_params(params):
    _, reply, call = run(LEGACY, "tools/call", params)
    assert reply["error"]["code"] == -32602 and call.error == -32602


def test_unknown_argument_keys_are_not_audited_by_name():
    _, _, call = run(LEGACY, "tools/call", {"name": "list_days", "arguments": {"secret words": 1, "limit": 2}})
    assert call.args == ("limit",)


def test_write_tool_only_when_enabled():
    e = env(writes=frozenset({"queue_add"}))
    _, reply, call = run(LEGACY, "tools/list", e=e)
    assert reply["result"]["tools"][-1]["name"] == "queue_add"
    _, reply, call = run(LEGACY, "tools/call", {"name": "queue_add", "arguments": {"project": "cpa", "text": "x"}},
                         e=e)  # fmt: skip
    assert reply["result"]["structuredContent"]["qid"] == "q1" and call.sizes == {"text_len": 1}


QUEUE_ADD = {"name": "queue_add", "arguments": {"project": "cpa", "text": "修一下"}}


def test_a_write_tool_announces_its_intent_before_it_runs():
    seen = []
    e = env(writes=frozenset({"queue_add"}))
    e = replace(e, intent=lambda call: seen.append((call, list(e.ctx.writer.added))))
    _, reply, call = run(LEGACY, "tools/call", QUEUE_ADD, e=e)
    assert reply["result"]["isError"] is False and e.ctx.writer.added == [("cpa", "修一下")]
    [(intent, added_before)] = seen
    assert added_before == [] and intent.phase == "intent" and intent.tool == "queue_add"
    assert intent.ids == ("cpa",) and intent.sizes == {"text_len": 9} and intent.args == ("project", "text")
    assert call.phase == "outcome" and call.error is None


def test_a_write_tool_does_not_run_when_its_intent_cannot_be_audited():
    def refuse(call):
        raise AuditError("disk full")

    e = env(writes=frozenset({"queue_add"}))
    e = replace(e, intent=refuse)
    _, reply, call = run(LEGACY, "tools/call", QUEUE_ADD, e=e)
    assert json.loads(reply["result"]["content"][0]["text"])["error"]["code"] == "E_AUDIT"
    assert e.ctx.writer.added == [] and call.error == "E_AUDIT"


def test_an_invalid_write_is_announced_without_ids_and_never_runs():
    seen = []
    e = env(writes=frozenset({"queue_add"}))
    e = replace(e, intent=seen.append)
    _, reply, call = run(LEGACY, "tools/call", {"name": "queue_add", "arguments": {"project": "cpa", "text": "-rf"}},
                         e=e)  # fmt: skip
    assert [(c.phase, c.ids, dict(c.sizes)) for c in seen] == [("intent", (), {})]
    assert call.error == "E_BAD_ARG" and e.ctx.writer.added == []


def test_read_tools_announce_nothing():
    seen = []
    e = replace(env(), intent=seen.append)
    run(LEGACY, "tools/call", {"name": "get_status"}, e=e)
    assert seen == []


def test_disabled_read_tool_is_gone():
    e = env(disabled=frozenset({"search"}))
    _, reply, _ = run(LEGACY, "tools/call", {"name": "search", "arguments": {"query": "x"}}, e=e)
    assert reply["error"]["code"] == -32602


def test_read_rate_limit_for_tools():
    state, e = LEGACY, env()
    for _ in range(20):
        state, reply, _ = run(state, "tools/call", {"name": "get_status"}, e=e)
        assert reply["result"]["isError"] is False
    state, reply, call = run(state, "tools/call", {"name": "get_status"}, e=e)
    body = json.loads(reply["result"]["content"][0]["text"])
    assert body["error"]["code"] == "E_RATE_LIMITED" and body["error"]["retry_after_s"] == 1.0
    assert call.error == "E_RATE_LIMITED"
    state, reply, _ = run(state, "tools/call", {"name": "get_status"}, e=e, now=1.0)
    assert reply["result"]["isError"] is False


def test_write_rate_limit_is_separate_and_tighter():
    state, e = LEGACY, env(writes=frozenset({"queue_add"}))
    params = {"name": "queue_add", "arguments": {"project": "cpa", "text": "x"}}
    codes = []
    for _ in range(4):
        state, reply, call = run(state, "tools/call", params, e=e)
        codes.append(call.error)
    assert codes == [None, None, None, "E_RATE_LIMITED"]
    assert state.read is None


def test_resources_rate_limit_is_internal_error():
    state = replace(LEGACY, read=dispatch.Bucket(0.0, 0.0))
    _, reply, _ = run(state, "resources/list", {})
    assert reply["error"]["code"] == -32603 and reply["error"]["data"]["retry_after_s"] == 1.0


def test_handshake_and_discover_are_not_rate_limited():
    state = replace(State(), read=dispatch.Bucket(0.0, 0.0))
    assert "result" in run(state, "server/discover", modern())[1]
    assert "result" in run(state, "initialize", {"protocolVersion": "2025-11-25"})[1]


def test_resources_list_templates_and_read():
    _, reply, call = run(LEGACY, "resources/list")
    assert reply["result"]["resources"] and reply["result"]["ttlMs"] == 0 and call.n > 0
    _, reply, _ = run(LEGACY, "resources/templates/list")
    assert len(reply["result"]["resourceTemplates"]) == 3
    _, reply, call = run(LEGACY, "resources/read", {"uri": "cpa://notes/2026-10-21"})
    assert reply["result"]["contents"][0]["text"] and call.ids == ("2026-10-21",)


@pytest.mark.parametrize(
    ("params", "ids"),
    [({"uri": "file:///etc/passwd"}, ()), ({}, ()), ({"uri": 3}, ()),
     ({"uri": "cpa://notes/2001-01-01"}, ("2001-01-01",))],
)  # fmt: skip
def test_resources_read_errors(params, ids):
    _, reply, call = run(LEGACY, "resources/read", params)
    assert reply["error"]["code"] == -32602 and call.ids == ids


def test_resources_list_bad_cursor():
    assert run(LEGACY, "resources/list", {"cursor": 5})[1]["error"]["code"] == -32602
    assert run(LEGACY, "resources/list", {"cursor": "zz"})[1]["error"]["code"] == -32602


def test_unexpected_exception_is_internal_error_without_details():
    e = env()
    broken = Env(ctx=replace(e.ctx, status=lambda: 1 / 0), tools=e.tools)
    _, reply, call = run(LEGACY, "tools/call", {"name": "get_status"}, e=broken)
    assert reply["error"] == {"code": -32603, "message": "Internal error"} and call.error == "internal"


def test_calls_are_counted():
    state, _, _ = run(LEGACY, "tools/list")
    state, _, _ = run(state, "ping")
    assert state.calls == 2


def test_audit_failure_replies():
    tool = dispatch.audit_failed(Request(1, "tools/call", {"name": "get_status"}))
    assert json.loads(tool["result"]["content"][0]["text"])["error"]["code"] == "E_AUDIT"
    assert tool["result"]["isError"] is True
    other = dispatch.audit_failed(Request(2, "resources/read", {}))
    assert other["error"]["code"] == -32603


def test_a_done_write_whose_outcome_was_not_audited_still_reports_what_it_did():
    e = env(writes=frozenset({"queue_add"}))
    req = Request(1, "tools/call", QUEUE_ADD)
    _, reply, call = dispatch.handle(LEGACY, req, e, 0.0)
    out = dispatch.audit_failed(req, reply, call)["result"]
    assert out["isError"] is False and out["structuredContent"]["qid"] == "q1"
    assert "audit" in out["structuredContent"]["warning"]
    assert json.loads(out["content"][0]["text"]) == out["structuredContent"]


def test_a_failed_write_whose_outcome_was_not_audited_is_an_audit_error():
    e = env(writes=frozenset({"queue_add"}))
    req = Request(1, "tools/call", {"name": "queue_add", "arguments": {"project": "nope", "text": "x"}})
    _, reply, call = dispatch.handle(LEGACY, req, e, 0.0)
    out = dispatch.audit_failed(req, reply, call)["result"]
    assert out["isError"] is True and "E_AUDIT" in out["content"][0]["text"]
