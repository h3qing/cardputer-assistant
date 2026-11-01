"""cpa filing triage end to end (docs/23 §7.2): the real command in a child process on a pseudo-terminal, over a
temporary $CPA_HOME and archive; the script waits for each screen, presses a key, and reads events.jsonl after."""

from __future__ import annotations

import json
import os
import pty
import select
import subprocess
import sys
import time

import pytest

from cpad.config.settings import Paths
from tests.support import archive_tree as at
from tests.support import filing_home

pytestmark = pytest.mark.usefixtures("zh")  # the words below are Chinese; an English case takes the en fixture

WAIT_S = 20.0
CPA = "import sys; from cpad.cli.main import main; sys.exit(main(['filing', 'triage']))"


def _until(fd: int, text: str) -> str:
    """What the child printed, up to and including `text`; fails after WAIT_S."""
    seen, end = b"", time.monotonic() + WAIT_S
    while text not in seen.decode("utf-8", errors="ignore") and time.monotonic() < end:
        if select.select([fd], [], [], end - time.monotonic())[0]:
            try:
                chunk = os.read(fd, 4096)
            except OSError:  # Linux: the child closed its side
                break
            if not chunk:
                break
            seen += chunk
    shown = seen.decode("utf-8", errors="replace")
    assert text in shown, f"no {text!r} in {shown!r}"
    return shown


def _rest(fd: int) -> str:
    """Everything else the child prints until it exits: it gives the terminal back only once its output is read."""
    seen, end = b"", time.monotonic() + WAIT_S
    while time.monotonic() < end and select.select([fd], [], [], end - time.monotonic())[0]:
        try:
            chunk = os.read(fd, 4096)
        except OSError:  # the child closed its side
            break
        if not chunk:
            break
        seen += chunk
    return seen.decode("utf-8", errors="replace")


def _triage(paths: Paths, keys: list[tuple[bytes, str]]) -> list[str]:
    """Run it, wait for the first screen, then write each key and wait for its text; Escape; what it printed."""
    master, slave = pty.openpty()
    child = subprocess.Popen([sys.executable, "-c", CPA], stdin=slave, stdout=slave, stderr=slave,
                             env={**os.environ, "CPA_HOME": str(paths.home)}, start_new_session=True)  # fmt: skip
    os.close(slave)
    try:
        shown = [_until(master, "1 cpa  2 blog")]
        for key, text in keys:
            os.write(master, key)
            shown.append(_until(master, text))
        os.write(master, b"\x1b")
        shown.append(_until(master, "退出") + _rest(master))
        assert child.wait(timeout=WAIT_S) == 0
    finally:
        child.kill()
        os.close(master)
    return shown


def _events(paths: Paths) -> list[dict]:
    return [json.loads(line) for line in paths.events.read_bytes().split(b"\n") if line]


def test_triage_files_one_and_quits_in_a_real_terminal(short_tmp, tmp_path):
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    first, second, last = _triage(paths, [(b"2", "2/3")])
    assert "1/3  便条  10-21 15:05  0:24" in first and "→ 2 blog（u 撤销）" in second and "这次做了 1 件" in last
    events = _events(paths)
    assert (events[-1]["ev"], events[-1]["rec"], events[-1]["project"]) == ("rec.edited", at.MEMO, "blog")


def test_a_double_press_files_only_the_row_on_screen(short_tmp, tmp_path):
    """22 in one go: the second 2 was typed before the next row was shown, so it is thrown away."""
    paths = filing_home.build(short_tmp / "home", tmp_path / "notes")
    before = len(_events(paths))
    _, second, last = _triage(paths, [(b"22", "2/3")])
    assert [e["rec"] for e in _events(paths)[before:]] == [at.MEMO]
    assert "3/3" not in second + last and "这次做了 1 件" in last
