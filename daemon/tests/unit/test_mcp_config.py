"""$CPA_HOME/mcp.json (docs/15 §3.10): strict, read-only by default, writes enabled one by one."""

from __future__ import annotations

import json
import os

import pytest

from cpad.mcp import config
from cpad.mcp.config import McpConfig, McpConfigError


def _write(path, doc, mode=0o600):
    path.write_text(json.dumps(doc) if not isinstance(doc, str) else doc)
    path.chmod(mode)
    return path


def test_missing_file_is_read_only_default(tmp_path):
    assert config.load(tmp_path / "mcp.json") == McpConfig()
    assert McpConfig().write_tools == frozenset() and McpConfig().disabled_tools == frozenset()


def test_full_file(tmp_path):
    path = _write(tmp_path / "mcp.json", {"version": 1, "write_tools": ["queue_add"], "disabled_tools": ["search"]})
    assert config.load(path) == McpConfig(frozenset({"queue_add"}), frozenset({"search"}))


@pytest.mark.parametrize(
    ("doc", "words"),
    [
        ([], "object"),
        ({"version": 2}, "version"),
        ({}, "version"),
        ({"version": 1, "write_tool": ["queue_add"]}, "unknown key"),
        ({"version": 1, "write_tools": "queue_add"}, "list"),
        ({"version": 1, "write_tools": ["queue_add", "queue_add"]}, "twice"),
        ({"version": 1, "write_tools": ["search"]}, "unknown tool"),
        ({"version": 1, "write_tools": ["add_note"]}, "not available yet"),
        ({"version": 1, "disabled_tools": ["queue_add"]}, "unknown tool"),
        ({"version": 1, "disabled_tools": [1]}, "list"),
        ("{nope", "JSON"),
    ],
)
def test_strict_parse_refuses_to_start(tmp_path, doc, words):
    path = _write(tmp_path / "mcp.json", doc)
    with pytest.raises(McpConfigError) as err:
        config.load(path)
    assert words in str(err.value)


def test_loose_permissions_are_tightened(tmp_path):
    path = _write(tmp_path / "mcp.json", {"version": 1}, mode=0o644)
    config.load(path)
    assert path.stat().st_mode & 0o777 == 0o600


def test_symlink_is_refused(tmp_path):
    real = _write(tmp_path / "real.json", {"version": 1, "write_tools": ["queue_add"]})
    link = tmp_path / "mcp.json"
    link.symlink_to(real)
    with pytest.raises(McpConfigError, match="regular file"):
        config.load(link)


def test_directory_is_refused(tmp_path):
    (tmp_path / "mcp.json").mkdir()
    with pytest.raises(McpConfigError, match="regular file"):
        config.load(tmp_path / "mcp.json")


def test_oversized_file_is_refused(tmp_path):
    path = _write(tmp_path / "mcp.json", '{"version": 1}' + " " * config.FILE_MAX)
    with pytest.raises(McpConfigError, match="too large"):
        config.load(path)


def test_config_error_is_a_value_error_so_cpa_exits_2():
    assert issubclass(McpConfigError, ValueError)


def test_unreadable_file_is_an_error(tmp_path):
    path = _write(tmp_path / "mcp.json", {"version": 1}, mode=0o000)
    if os.access(path, os.R_OK):
        pytest.skip("running as a user that can read anything")
    with pytest.raises(McpConfigError):
        config.load(path)
