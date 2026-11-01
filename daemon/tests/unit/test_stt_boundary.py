"""Boundaries (docs/22 §4.1-§4.3, ADR 0004): cpad keeps no transcription code and never imports cpa_stt;
cpa_stt uses only cpad's small helpers (config files, validators, the contract reader, projects, launchd, i18n),
never the daemon; it opens no sockets, fetches nothing and never runs a shell."""

from __future__ import annotations

import ast
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "src"
HELPERS = {"cpad", "cpad.config.files", "cpad.config.settings", "cpad.protocol.validate", "cpad.recordings.contract",
           "cpad.projects.registry", "cpad.service", "cpad.service.launchd", "cpad.i18n"}  # fmt: skip
NETWORK = {"socket", "ssl", "urllib", "http", "ftplib", "smtplib", "asyncio"}


def _imports(path: Path) -> set[str]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    found = {a.name for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names}
    return found | {n.module for n in ast.walk(tree) if isinstance(n, ast.ImportFrom) and n.module}


def test_cpad_never_imports_cpa_stt():
    offenders = [p.name for p in (SRC / "cpad").rglob("*.py") if any(m.split(".")[0] == "cpa_stt" for m in _imports(p))]
    assert offenders == []


def test_cpa_stt_uses_only_cpads_helpers():
    used = {m for p in (SRC / "cpa_stt").rglob("*.py") for m in _imports(p) if m.split(".")[0] == "cpad"}
    assert used <= HELPERS, used - HELPERS


def test_cpa_stt_opens_no_network_and_runs_no_shell():
    for path in (SRC / "cpa_stt").rglob("*.py"):
        assert not {m.split(".")[0] for m in _imports(path)} & NETWORK, path.name
        text = path.read_text(encoding="utf-8")
        assert "shell=True" not in text and "os.system" not in text and "os.popen" not in text, path.name


def test_no_cpa_stt_module_finds_files_through_its_own_path():
    """Like cpad (gh #57): package data (the whisper.cpp example) goes through importlib.resources."""
    from tests.unit.test_packaged_limits import path_lookups  # noqa: PLC0415

    for path in sorted((SRC / "cpa_stt").rglob("*.py")):
        assert not path_lookups(path.read_text(encoding="utf-8")), path.name
