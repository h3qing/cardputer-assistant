"""$CPA_HOME/mcp.json (docs/15 §3.10): {"version": 1, "write_tools": [], "disabled_tools": []}.

No file means read tools on, write tools off. Parsing is strict: an unknown key, an unknown tool or a
wrong type stops the server (exit 2), so a typo can never quietly widen what clients may do. Write
tools are enabled only here, never by a launch flag. A loose file mode is tightened to 0600.
"""

from __future__ import annotations

import json
import os
import stat
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from cpad.mcp.catalog import PLANNED_WRITES, READ_NAMES, WRITE_NAMES

FILE_NAME = "mcp.json"
FILE_MAX = 64 * 1024
KEYS = ("version", "write_tools", "disabled_tools")


class McpConfigError(ValueError):
    """mcp.json is invalid; the server does not start."""


@dataclass(frozen=True, slots=True)
class McpConfig:
    write_tools: frozenset[str] = field(default_factory=frozenset)
    disabled_tools: frozenset[str] = field(default_factory=frozenset)


def _names(value: Any, key: str, allowed: tuple[str, ...]) -> frozenset[str]:
    if not isinstance(value, list) or not all(isinstance(n, str) for n in value):
        raise McpConfigError(f"mcp.json: {key} must be a list of tool names")
    if len(set(value)) != len(value):
        raise McpConfigError(f"mcp.json: a tool is listed twice in {key}")
    for name in value:
        if name in PLANNED_WRITES and key == "write_tools":
            raise McpConfigError(f"mcp.json: write tool {name} is not available yet")
        if name not in allowed:
            raise McpConfigError(f"mcp.json: unknown tool {name!r} in {key}")
    return frozenset(value)


def parse(doc: Any) -> McpConfig:
    if not isinstance(doc, dict):
        raise McpConfigError("mcp.json must be a JSON object")
    unknown = sorted(set(doc) - set(KEYS))
    if unknown:
        raise McpConfigError(f"mcp.json: unknown key {unknown[0]!r}")
    if doc.get("version") != 1:
        raise McpConfigError('mcp.json: "version" must be 1')
    return McpConfig(
        write_tools=_names(doc.get("write_tools", []), "write_tools", WRITE_NAMES),
        disabled_tools=_names(doc.get("disabled_tools", []), "disabled_tools", READ_NAMES),
    )


def _read(fd: int) -> bytes:
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode):
        raise McpConfigError("mcp.json must be a regular file")
    if info.st_mode & 0o077:
        os.fchmod(fd, info.st_mode & 0o700)
    data = os.read(fd, FILE_MAX + 1)
    if len(data) > FILE_MAX:
        raise McpConfigError("mcp.json is too large")
    return data


def load(path: Path) -> McpConfig:
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return McpConfig()
    except OSError as exc:
        raise McpConfigError("mcp.json must be a regular file the user can read (not a symlink)") from exc
    try:
        data = _read(fd)
    finally:
        os.close(fd)
    try:
        doc = json.loads(data.decode("utf-8"))
    except ValueError as exc:
        raise McpConfigError("mcp.json is not valid JSON") from exc
    return parse(doc)
