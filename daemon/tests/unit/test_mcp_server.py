"""The stdio loop (docs/15 §3.2, §3.9): bounded lines, one reply per request, audit before reply, EOF exits."""

from __future__ import annotations

import io
import json

from cpad.archive.fs import Root
from cpad.config.settings import load_config
from cpad.mcp import backend, catalog, server
from cpad.mcp.audit import AuditError, AuditLog, Proc
from cpad.mcp.config import McpConfig
from cpad.mcp.dispatch import Env
from cpad.mcp.jsonrpc import LINE_MAX
from cpad.mcp.tools import Ctx
from tests.support import mcp_home
from tests.support.fake_reader import CANARY, FakeReader, fake_status

PROC = Proc(1, 0, "test")
HELLO = {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}
INIT = {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": HELLO}
DEFAULT = McpConfig()


class FailingAudit:
    def __init__(self, fail_from: int = 0):
        self.n, self.fail_from, self.records = 0, fail_from, []

    def append(self, rec):
        self.n += 1
        if self.n > self.fail_from:
            raise AuditError("disk full")
        self.records.append(rec)


class PickyAudit(AuditLog):
    """A real audit log that refuses the records `refuse` picks."""

    def __init__(self, path, refuse):
        object.__setattr__(self, "refuse", refuse)
        super().__init__(path)

    def append(self, rec):
        if self.refuse(rec):
            raise AuditError("disk full")
        super().append(rec)


WRITES = McpConfig(write_tools=frozenset({"queue_add"}))
MODERN = {"io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {}}
QUEUE_ADD = {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {"name": "queue_add", "arguments": {"project": "cpa", "text": "写一条"}, "_meta": MODERN},
}


def _write_env(base):
    paths = mcp_home.build(base)
    root = Root.at(load_config(paths.config).notes_path())
    return paths, Env(ctx=backend.context(paths, root, writes=True), tools=catalog.visible(WRITES))


def _env():
    return Env(ctx=Ctx(reader=FakeReader(), status=fake_status), tools=catalog.visible(McpConfig()))


def _lines(*docs) -> io.BytesIO:
    return io.BytesIO(b"".join(d if isinstance(d, bytes) else json.dumps(d).encode() + b"\n" for d in docs))


def _serve(inp, audit, cfg=DEFAULT):
    out = io.BytesIO()
    code = server.serve(inp, out, _env(), audit, PROC, cfg)
    return code, [json.loads(line) for line in out.getvalue().splitlines()]


def test_a_legacy_session_end_to_end(tmp_path):
    audit = AuditLog(tmp_path / "logs" / "mcp-audit.jsonl")
    search = {"name": "search", "arguments": {"query": "Bob"}}
    code, replies = _serve(_lines(
        INIT, {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": search},
        {"jsonrpc": "2.0", "id": 3, "method": "ping"}), audit)  # fmt: skip
    assert code == 0 and [r["id"] for r in replies] == [0, 1, 2, 3]
    records = [json.loads(line) for line in audit.path.read_text().splitlines()]
    assert [r.get("event") or r["method"] for r in records] == ["start", "initialize", "tools/list", "tools/call",
                                                                 "stop"]  # fmt: skip
    call = records[3]
    assert call["tool"] == "search" and call["qlen"] == 3 and call["bytes"] > 0 and call["client"]["name"] == "t"
    assert records[-1]["calls"] == 4


def test_parse_errors_and_oversized_lines_get_replies_and_the_loop_goes_on(tmp_path):
    big = b'{"jsonrpc":"2.0","id":9,"method":"ping","x":"' + b"a" * LINE_MAX + b'"}\n'
    code, replies = _serve(_lines(b"{oops\n", big, b"\n", INIT), AuditLog(tmp_path / "a.jsonl"))
    assert [r.get("error", {}).get("code") for r in replies] == [-32700, -32600, None]
    assert replies[1]["id"] is None and replies[2]["id"] == 0


def test_a_line_of_exactly_the_limit_is_accepted(tmp_path):
    head = b'{"jsonrpc":"2.0","id":1,"method":"ping","pad":"'
    line = head + b"a" * (LINE_MAX - len(head) - 2) + b'"}'
    assert len(line) == LINE_MAX
    _, replies = _serve(_lines(line + b"\n"), AuditLog(tmp_path / "a.jsonl"))
    assert replies == [{"jsonrpc": "2.0", "id": 1, "result": {}}]


def test_last_line_without_newline_is_still_handled(tmp_path):
    _, replies = _serve(io.BytesIO(b'{"jsonrpc":"2.0","id":5,"method":"ping"}'), AuditLog(tmp_path / "a.jsonl"))
    assert replies[0]["id"] == 5


def test_audit_failure_withholds_the_data():
    audit = FailingAudit(fail_from=2)
    _, replies = _serve(_lines(INIT, {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                      "params": {"name": "read_daily_note", "arguments": {"date": "2026-10-21"}}}),
                        audit)  # fmt: skip
    result = replies[1]["result"]
    assert result["isError"] is True and "E_AUDIT" in result["content"][0]["text"]
    assert CANARY not in json.dumps(replies)


def test_server_refuses_to_start_without_an_audit_log(capsys):
    code, replies = _serve(_lines(INIT), FailingAudit(fail_from=0))
    assert code == 1 and replies == []
    assert "audit" in capsys.readouterr().err


def test_broken_pipe_ends_quietly(tmp_path):
    class Closed(io.BytesIO):
        def write(self, data):
            raise BrokenPipeError

    code = server.serve(_lines(INIT, INIT), Closed(), _env(), AuditLog(tmp_path / "a.jsonl"), PROC, McpConfig())
    assert code == 0


def test_lines_discards_the_rest_of_an_oversized_line():
    inp = io.BytesIO(b"x" * (LINE_MAX * 2 + 10) + b"\nnext\n")
    assert list(server.lines(inp)) == [None, b"next"]


def _records(audit):
    return [json.loads(line) for line in audit.path.read_text().splitlines()]


def test_a_write_is_audited_before_and_after(tmp_path):
    paths, env = _write_env(tmp_path)
    audit = AuditLog(paths.logs / "mcp-audit.jsonl")
    out = io.BytesIO()
    server.serve(_lines(QUEUE_ADD), out, env, audit, PROC, WRITES)
    assert json.loads(out.getvalue())["result"]["structuredContent"]["qid"] == "q2"
    calls = [r for r in _records(audit) if r.get("method") == "tools/call"]
    assert [(r["phase"], r["tool"], r["text_len"], r["ids"]) for r in calls] == [
        ("intent", "queue_add", 9, ["cpa"]), ("outcome", "queue_add", 9, ["cpa"])]  # fmt: skip
    assert "写一条" not in audit.path.read_text()


def test_a_write_whose_intent_cannot_be_audited_changes_nothing(tmp_path):
    paths, env = _write_env(tmp_path)
    before = paths.events.read_bytes()
    audit = PickyAudit(paths.logs / "mcp-audit.jsonl", lambda rec: rec.get("method") == "tools/call")
    out = io.BytesIO()
    server.serve(_lines(QUEUE_ADD, {**QUEUE_ADD, "id": 2}), out, env, audit, PROC, WRITES)
    replies = [json.loads(line) for line in out.getvalue().splitlines()]
    assert all("E_AUDIT" in r["result"]["content"][0]["text"] for r in replies) and len(replies) == 2
    assert paths.events.read_bytes() == before


def test_a_write_whose_outcome_cannot_be_audited_still_names_its_qid(tmp_path):
    paths, env = _write_env(tmp_path)
    audit = PickyAudit(paths.logs / "mcp-audit.jsonl", lambda rec: rec.get("phase") == "outcome")
    out = io.BytesIO()
    server.serve(_lines(QUEUE_ADD), out, env, audit, PROC, WRITES)
    result = json.loads(out.getvalue())["result"]
    assert result["isError"] is False and result["structuredContent"]["qid"] == "q2"
    assert "audit" in result["structuredContent"]["warning"]
    assert [r["phase"] for r in _records(audit) if r.get("method") == "tools/call"] == ["intent"]
