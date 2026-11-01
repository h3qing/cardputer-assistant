"""Shared fixtures. AF_UNIX paths are limited to ~104 bytes on macOS, so sockets live in a short dir."""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path

import pytest

from tests.support.hermetic import (  # noqa: F401 (pytest finds hooks and fixtures by name)
    hermetic_home,
    pytest_configure,
    pytest_sessionfinish,
    pytest_terminal_summary,
    pytest_unconfigure,
)

LANGUAGE = ("CPA_LANG", "LC_ALL", "LC_MESSAGES", "LANG")


@pytest.fixture(autouse=True)
def _english_unless_a_test_says(monkeypatch):
    """No language set, whatever the shell running pytest says: English (cpad.i18n). A suite that pins the Chinese
    text uses the zh fixture (pytestmark = pytest.mark.usefixtures("zh")); an English case in it takes en as an argument
    (arguments are set up after usefixtures, so it wins)."""
    for name in LANGUAGE:
        monkeypatch.delenv(name, raising=False)


@pytest.fixture
def zh(monkeypatch):
    monkeypatch.setenv("CPA_LANG", "zh")


@pytest.fixture
def en(monkeypatch):
    monkeypatch.setenv("CPA_LANG", "en")


@pytest.fixture
def short_tmp():
    path = Path(tempfile.mkdtemp(prefix="cpa", dir="/tmp" if Path("/tmp").is_dir() else None))
    try:
        yield Path(path).resolve()
    finally:
        shutil.rmtree(path, ignore_errors=True)


@pytest.fixture(autouse=True)
def _never_open_apps_or_write_the_real_clipboard(monkeypatch):
    """docs/25 R1-C: no test may open a link or an app, or write the clipboard, for real. Fakes only."""
    import subprocess

    from cpad.dest import macos, pasteboard

    def refuse(*_args, **_kwargs):
        raise AssertionError("a test reached the real LaunchServices or NSPasteboard")

    for name in ("open_url", "open_app"):
        monkeypatch.setattr(macos.LaunchServices, name, refuse)
    for name in ("write", "clear"):
        monkeypatch.setattr(pasteboard.NativePasteboard, name, refuse)
    plain = pasteboard.ClipWriter._plain

    def guarded(self, text):
        if self._run is subprocess.run:
            refuse()
        return plain(self, text)

    monkeypatch.setattr(pasteboard.ClipWriter, "_plain", guarded)
