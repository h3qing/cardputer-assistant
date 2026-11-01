"""cpa mcp serve | tools [--json] | setup <client> | audit [--last N] [--json] (docs/15 §3.9, §3.10, §4).

`setup` only prints a snippet with this machine's paths filled in; it never writes any client's config.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
from typing import Any

from cpad.config.settings import Paths
from cpad.mcp import audit, catalog, server
from cpad.mcp import config as mcp_config

CLIENTS = ("claude-code", "codex", "cursor", "claude-desktop")
PLACEHOLDER = "/path/to/cpa"
NAME = "cardputer"
DESKTOP_CONFIG = "~/Library/Application Support/Claude/claude_desktop_config.json"


def _toml(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)  # a TOML basic string uses the same escapes


def _claude_code(cpa: str, home: str | None) -> str:
    # The name goes before -e: `-e, --env <env...>` is variadic and would swallow it; `--` ends the env list.
    env = f"-e CPA_HOME={shlex.quote(home)} " if home else ""
    return (f"# Claude Code, user scope (~/.claude.json): keep tool approvals on\n"
            f"claude mcp add --scope user {NAME} {env}-- {shlex.quote(cpa)} mcp serve\nclaude mcp list\n")  # fmt: skip


def _codex(cpa: str, home: str | None) -> str:
    lines = ["# Codex CLI: paste into ~/.codex/config.toml (your personal config, not a project's .codex/)",
             f"[mcp_servers.{NAME}]", f"command = {_toml(cpa)}", 'args = ["mcp", "serve"]',
             "startup_timeout_sec = 10", "tool_timeout_sec = 30"]  # fmt: skip
    if home:
        lines += [f"[mcp_servers.{NAME}.env]", f"CPA_HOME = {_toml(home)}"]
    return "\n".join(lines) + "\n"


def _json_client(cpa: str, home: str | None, where: str, stdio_type: bool) -> str:
    entry: dict[str, Any] = {"type": "stdio"} if stdio_type else {}
    entry |= {"command": cpa, "args": ["mcp", "serve"]}
    if home:
        entry["env"] = {"CPA_HOME": home}
    doc = {"mcpServers": {NAME: entry}}
    return f"Add to {where}:\n" + json.dumps(doc, ensure_ascii=False, indent=2) + "\n"


def snippet(client: str, cpa_path: str | None, cpa_home: str | None) -> str:
    """The setup text for one client. Pure: paths in, text out."""
    cpa = cpa_path or PLACEHOLDER
    if client == "claude-code":
        return _claude_code(cpa, cpa_home)
    if client == "codex":
        return _codex(cpa, cpa_home)
    if client == "cursor":
        return _json_client(cpa, cpa_home, "~/.cursor/mcp.json (global, not a project's .cursor/)", True)
    return _json_client(cpa, cpa_home, f"{DESKTOP_CONFIG}, then restart Claude Desktop", False)


def _serve(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    return server.run(paths)


def _tools(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    shown = catalog.visible(mcp_config.load(paths.home / mcp_config.FILE_NAME))
    if args.json:
        print(json.dumps([t.listing() for t in shown], ensure_ascii=False, indent=2))
        return 0
    for tool in shown:
        print(f"{tool.name:<16} {'write' if tool.write else 'read':<5} {tool.title}")
    return 0


def _setup(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    print(snippet(args.client, deps.which("cpa"), os.environ.get("CPA_HOME") or None), end="")
    return 0


def _line(rec: dict[str, Any]) -> str:
    client = (rec.get("client") or {}).get("name") or "-"
    what = rec.get("event") or f"{rec.get('method')} {rec.get('tool') or ''}".strip()
    return (f"{rec.get('at', '-')}  {client:<20} parent={rec.get('parent') or '-'}  {what}  n={rec.get('n', '-')}"
            f" bytes={rec.get('bytes', '-')} error={rec.get('error') or '-'}")  # fmt: skip


def _audit(paths: Paths, args: argparse.Namespace, deps: Any) -> int:
    records = audit.tail(paths.logs / audit.FILE_NAME, args.last)
    if args.json:
        print("\n".join(json.dumps(r, ensure_ascii=False) for r in records))
    elif not records:
        print("no MCP calls recorded")
    else:
        print("\n".join(_line(r) for r in records))
    return 0


def add_parsers(sub: Any) -> None:
    mcp = sub.add_parser("mcp", help="local MCP server for AI clients (stdio, read-only by default, audited)")
    actions = mcp.add_subparsers(dest="action", required=True)
    actions.add_parser("serve", help="speak MCP on stdin/stdout (started by the client)").set_defaults(fn=_serve)
    tools = actions.add_parser("tools", help="the tools this server offers with the current mcp.json")
    tools.add_argument("--json", action="store_true")
    tools.set_defaults(fn=_tools)
    setup = actions.add_parser("setup", help="print the config snippet for a client (writes nothing)")
    setup.add_argument("client", choices=CLIENTS)
    setup.set_defaults(fn=_setup)
    log = actions.add_parser("audit", help="recent MCP calls (no content is ever recorded)")
    log.add_argument("--last", type=int, default=50)
    log.add_argument("--json", action="store_true")
    log.set_defaults(fn=_audit)
