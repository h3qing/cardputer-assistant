"""ADR 0004 rule 1 and 2, statically: no new runtime dependency, and nothing in cpa mcp can listen."""

from __future__ import annotations

import ast
import tomllib
from pathlib import Path

import cpad.mcp

PACKAGE = Path(cpad.mcp.__file__).parent
PYPROJECT = Path(__file__).resolve().parents[2] / "pyproject.toml"
FORBIDDEN_MODULES = {"http", "socketserver", "asyncio", "xmlrpc", "ssl", "urllib", "socket", "mcp"}
FORBIDDEN_CALLS = {"bind", "listen", "start_server", "create_server", "serve_forever", "urlopen"}


def _sources():
    return sorted(PACKAGE.glob("*.py"))


def test_no_network_imports_in_cpad_mcp():
    for path in _sources():
        tree = ast.parse(path.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            names = [a.name for a in node.names] if isinstance(node, ast.Import) else []
            names += [node.module or ""] if isinstance(node, ast.ImportFrom) else []
            assert not {n.split(".")[0] for n in names} & FORBIDDEN_MODULES, (path.name, names)


def test_no_listening_calls_in_cpad_mcp():
    for path in _sources():
        tree = ast.parse(path.read_text(encoding="utf-8"))
        called = {n.func.attr for n in ast.walk(tree) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
        assert not called & FORBIDDEN_CALLS, (path.name, called & FORBIDDEN_CALLS)


def test_runtime_dependencies_are_still_pyserial_only():
    doc = tomllib.loads(PYPROJECT.read_text(encoding="utf-8"))
    assert doc["project"]["dependencies"] == ["pyserial==3.5"]
