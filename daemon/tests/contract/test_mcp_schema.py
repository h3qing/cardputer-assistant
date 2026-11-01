"""Both protocol eras against the vendored official MCP schemas (docs/15 §3.3, §8; ADR 0004 rule 3).

The golden transcripts run through the real dispatcher over a real temporary archive. Every reply must
satisfy the official $def named next to its request, every tool result its own outputSchema, and no
structured result may exceed the output budget.
"""

from __future__ import annotations

import json

import jsonschema
import pytest

from cpad.archive.fs import Root
from cpad.config.settings import load_config
from cpad.mcp import backend, catalog, dispatch
from cpad.mcp.config import McpConfig
from cpad.mcp.dispatch import Env, State
from cpad.mcp.jsonrpc import Request
from cpad.mcp.limits import OUTPUT_MAX, json_size
from tests.support import filing_home, mcp_home

ERAS = {"golden-legacy.jsonl": "2025-11-25", "golden-modern.jsonl": "2026-07-28"}


@pytest.fixture(scope="module")
def env(tmp_path_factory):
    paths = mcp_home.build(tmp_path_factory.mktemp("mcp"))
    root = Root.at(load_config(paths.config).notes_path())
    return Env(ctx=backend.context(paths, root), tools=catalog.visible(McpConfig()))


def _validator(version: str, name: str) -> jsonschema.Draft202012Validator:
    doc = mcp_home.schema(version)
    return jsonschema.Draft202012Validator({**doc, "$ref": f"#/$defs/{name}"})


def _check(validator_version: str, name: str, value) -> None:
    errors = sorted(_validator(validator_version, name).iter_errors(value), key=str)
    assert not errors, f"{name}: {errors[0].message} at {list(errors[0].absolute_path)}"


def _replies(env: Env, transcript: str):
    state = State()
    for step in mcp_home.golden(transcript):
        raw = step["request"]
        if "id" not in raw:
            continue
        state, reply, _ = dispatch.handle(state, Request(raw["id"], raw["method"], raw.get("params", {})), env, 0.0)
        yield step, reply


@pytest.mark.parametrize("transcript", ERAS)
def test_every_reply_satisfies_the_official_schema(env, transcript):
    version = ERAS[transcript]
    for step, reply in _replies(env, transcript):
        assert json.loads(json.dumps(reply)) == reply
        if step.get("result"):
            assert "result" in reply, (step["request"]["method"], reply)
            _check(version, "JSONRPCResultResponse", reply)
            _check(version, step["result"], reply["result"])
        else:
            assert reply["error"]["code"] == step["error"], (step["request"], reply)
            _check(version, step.get("def", "JSONRPCErrorResponse"), reply)


@pytest.mark.parametrize("transcript", ERAS)
def test_tool_results_satisfy_their_output_schemas_and_budget(env, transcript):
    schemas = {t.name: t.output_schema for t in catalog.READ_TOOLS}
    checked = 0
    for step, reply in _replies(env, transcript):
        result = reply.get("result", {})
        if step["request"]["method"] != "tools/call" or result.get("isError") is not False:
            continue
        structured = result["structuredContent"]
        jsonschema.validate(structured, schemas[step["request"]["params"]["name"]])
        assert json_size(structured) <= OUTPUT_MAX and structured["trust"] == "untrusted-user-data"
        assert json.loads(result["content"][0]["text"]) == structured
        checked += 1
    assert checked == 11


@pytest.mark.parametrize("version", ["2025-11-25", "2025-06-18"])
def test_both_handshake_versions_work(env, version):
    params = {"protocolVersion": version, "capabilities": {}, "clientInfo": {"name": "c", "version": "1"}}
    _, reply, _ = dispatch.handle(State(), Request(1, "initialize", params), env, 0.0)
    assert reply["result"]["protocolVersion"] == version
    _check("2025-11-25", "InitializeResult", reply["result"])


def test_write_tool_listing_satisfies_both_schemas(env):
    writes = Env(ctx=env.ctx, tools=catalog.visible(McpConfig(write_tools=frozenset({"queue_add"}))))
    _, reply, _ = dispatch.handle(dispatch.State(), Request(1, "tools/list", {"_meta": {
        "io.modelcontextprotocol/protocolVersion": "2026-07-28",
        "io.modelcontextprotocol/clientCapabilities": {}}}), writes, 0.0)  # fmt: skip
    for version in ERAS.values():
        _check(version, "ListToolsResult", reply["result"])


def test_audit_failure_reply_satisfies_both_schemas():
    tool = dispatch.audit_failed(Request(1, "tools/call", {}))
    other = dispatch.audit_failed(Request(2, "resources/read", {}))
    for version in ERAS.values():
        _check(version, "CallToolResult", tool["result"])
        _check(version, "JSONRPCErrorResponse", other)


def test_an_unaudited_done_write_satisfies_both_schemas_and_its_output_schema(tmp_path):
    paths = mcp_home.build(tmp_path)
    root = Root.at(load_config(paths.config).notes_path())
    writes = Env(ctx=backend.context(paths, root, writes=True),
                 tools=catalog.visible(McpConfig(write_tools=frozenset({"queue_add"}))))  # fmt: skip
    req = Request(
        1,
        "tools/call",
        {
            "name": "queue_add",
            "arguments": {"project": "cpa", "text": "x"},
            "_meta": {
                "io.modelcontextprotocol/protocolVersion": "2026-07-28",
                "io.modelcontextprotocol/clientCapabilities": {},
            },
        },
    )
    _, reply, call = dispatch.handle(State(), req, writes, 0.0)
    result = dispatch.audit_failed(req, reply, call)["result"]
    jsonschema.validate(result["structuredContent"], catalog.WRITE_TOOLS[0].output_schema)
    for version in ERAS.values():
        _check(version, "CallToolResult", result)


@pytest.mark.parametrize(
    ("name", "arguments"),
    [("list_unfiled", {}), ("list_unfiled", {"filing": "all", "orphans": True}),
     ("get_filing_item", {"id": "3QZK1M7-00042-0A1F"}), ("get_filing_item", {"id": "3QZK1M7-00099-0000"})],
)  # fmt: skip
def test_filing_tools_satisfy_both_schemas_and_their_output_schemas(short_tmp, tmp_path, name, arguments):
    """docs/23 §3.5, on a filing archive (the golden transcripts already fill the read burst)."""
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    env = Env(ctx=backend.context(paths, Root.at(load_config(paths.config).notes_path())),
              tools=catalog.visible(McpConfig()))  # fmt: skip
    meta = {"io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {}}
    req = Request(1, "tools/call", {"name": name, "arguments": arguments, "_meta": meta})
    _, reply, _ = dispatch.handle(State(), req, env, 0.0)
    result = reply["result"]
    for version in ERAS.values():
        _check(version, "CallToolResult", result)
    if not result["isError"]:
        jsonschema.validate(result["structuredContent"], catalog.find(catalog.READ_TOOLS, name).output_schema)
        assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
