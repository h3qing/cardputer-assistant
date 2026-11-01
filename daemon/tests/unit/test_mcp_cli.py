"""cpa mcp serve | tools | setup <client> | audit (docs/15 §3.9, §3.10, §4). setup never writes client configs."""

from __future__ import annotations

import json
import tomllib

import pytest

from cpad.cli.main import CliDeps
from cpad.cli.main import main as cpa
from cpad.mcp import cli as mcp_cli
from cpad.mcp.audit import AuditLog

CPA = "/Users/me/.local/bin/cpa"


@pytest.fixture
def home(short_tmp, monkeypatch):
    path = short_tmp / "home"
    path.mkdir(mode=0o700)
    monkeypatch.setenv("CPA_HOME", str(path))
    return path


def _deps():
    return CliDeps(which=lambda name, path=None: CPA if name == "cpa" else None)


def test_tools_lists_read_tools_by_default(home, capsys):
    assert cpa(["mcp", "tools"]) == 0
    out = capsys.readouterr().out.splitlines()
    assert len(out) == 12 and out[0].startswith("list_days") and all(" read " in line for line in out)


def test_tools_json_shows_enabled_writes(home, capsys):
    (home / "mcp.json").write_text('{"version": 1, "write_tools": ["queue_add"]}')
    assert cpa(["mcp", "tools", "--json"]) == 0
    listed = json.loads(capsys.readouterr().out)
    assert listed[-1]["name"] == "queue_add" and listed[-1]["annotations"]["readOnlyHint"] is False


def test_bad_mcp_json_exits_2(home, capsys):
    (home / "mcp.json").write_text('{"version": 1, "write_tool": []}')
    assert cpa(["mcp", "tools"]) == 2
    assert "unknown key" in capsys.readouterr().err


def test_serve_with_bad_mcp_json_exits_2_before_reading_stdin(home, capsys):
    (home / "mcp.json").write_text('{"version": 1, "write_tools": ["rm_rf"]}')
    assert cpa(["mcp", "serve"]) == 2
    assert "unknown tool" in capsys.readouterr().err


def test_setup_claude_code(home, capsys, monkeypatch):
    monkeypatch.delenv("CPA_HOME")
    assert cpa(["mcp", "setup", "claude-code"], _deps()) == 0
    out = capsys.readouterr().out
    assert f"claude mcp add --scope user cardputer -- {CPA} mcp serve" in out and "CPA_HOME" not in out


def test_setup_codex_is_valid_toml_with_cpa_home(home, capsys):
    assert cpa(["mcp", "setup", "codex"], _deps()) == 0
    out = capsys.readouterr().out
    doc = tomllib.loads("\n".join(line for line in out.splitlines() if not line.startswith("#")))
    server = doc["mcp_servers"]["cardputer"]
    assert server["command"] == CPA and server["args"] == ["mcp", "serve"] and server["env"]["CPA_HOME"] == str(home)


@pytest.mark.parametrize(("client", "where"), [("cursor", "~/.cursor/mcp.json"),
                                               ("claude-desktop", "claude_desktop_config.json")])  # fmt: skip
def test_setup_json_clients(home, capsys, client, where):
    assert cpa(["mcp", "setup", client], _deps()) == 0
    head, _, body = capsys.readouterr().out.partition("\n")
    assert where in head
    server = json.loads(body)["mcpServers"]["cardputer"]
    assert server["command"] == CPA and server["args"] == ["mcp", "serve"]


def test_setup_writes_nothing(home, tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("HOME", str(tmp_path))
    before = sorted(p.name for p in tmp_path.rglob("*"))
    for client in mcp_cli.CLIENTS:
        cpa(["mcp", "setup", client], _deps())
    assert sorted(p.name for p in tmp_path.rglob("*")) == before


def test_setup_without_an_installed_cpa_shows_a_placeholder(home, capsys):
    assert cpa(["mcp", "setup", "cursor"], CliDeps(which=lambda name, path=None: None)) == 0
    assert "/path/to/cpa" in capsys.readouterr().out


def test_claude_code_puts_the_name_before_the_variadic_env_option():
    out = mcp_cli.snippet("claude-code", "/Users/me/my tools/cpa", "/x y")
    assert "claude mcp add --scope user cardputer -e CPA_HOME='/x y' -- '/Users/me/my tools/cpa' mcp serve\n" in out


def test_snippet_quotes_odd_paths():
    out = mcp_cli.snippet("claude-code", "/Users/me/my tools/cpa", "/Users/me/Library/Application Support/x")
    assert "'/Users/me/my tools/cpa'" in out and "CPA_HOME='/Users/me/Library/Application Support/x'" in out


def test_audit_listing(home, capsys):
    log = AuditLog(home / "logs" / "mcp-audit.jsonl")
    log.append({"at": "2026-10-21T15:30:02.120+08:00", "event": "start", "pid": 1, "write_tools": []})
    log.append({"at": "2026-10-21T15:30:03.000+08:00", "method": "tools/call", "tool": "search",
                "client": {"name": "codex-mcp-client", "version": "1"}, "parent": "codex", "n": 2, "bytes": 99,
                "error": None, "era": "legacy"})  # fmt: skip
    assert cpa(["mcp", "audit", "--last", "5"]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert "start" in lines[0] and "codex-mcp-client" in lines[1] and "search" in lines[1]
    assert cpa(["mcp", "audit", "--json", "--last", "1"]) == 0
    assert json.loads(capsys.readouterr().out)["tool"] == "search"


def test_audit_with_no_log(home, capsys):
    assert cpa(["mcp", "audit"]) == 0
    assert "no MCP calls recorded" in capsys.readouterr().out
