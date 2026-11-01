"""Hermetic runs (#71): the daemon tests' plugin, loaded by path (tools/hil is its own import root)."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_PATH = Path(__file__).resolve().parents[3] / "daemon" / "tests" / "support" / "hermetic.py"
_SPEC = importlib.util.spec_from_file_location("cpa_hermetic", _PATH)
hermetic = importlib.util.module_from_spec(_SPEC)
sys.modules["cpa_hermetic"] = hermetic
_SPEC.loader.exec_module(hermetic)

hermetic_home = hermetic.hermetic_home
pytest_configure = hermetic.pytest_configure
pytest_unconfigure = hermetic.pytest_unconfigure
pytest_sessionfinish = hermetic.pytest_sessionfinish
pytest_terminal_summary = hermetic.pytest_terminal_summary
