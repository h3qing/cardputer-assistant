"""The clipboard is cleared 2 minutes after a write from a short-lived cpa process too (docs/25 §5.3): cpa hands the
clear to a detached `python -m cpad.dest.clip_clear COUNT`, whose argv holds only the changeCount, never text."""

from __future__ import annotations

import subprocess
import sys

import pytest

from cpad.dest import clip_clear
from cpad.dest.pasteboard import CLEAR_AFTER_S, ClipWriter


class Board:
    def __init__(self, count: int = 0) -> None:
        self.count, self.cleared = count, 0

    def write(self, text: str) -> int:
        self.count += 1
        return self.count

    def change_count(self) -> int:
        return self.count

    def clear(self) -> None:
        self.cleared += 1


def test_a_writer_with_clear_later_hands_over_the_count_instead_of_a_timer():
    handed: list[int] = []

    def no_timer(_delay, _fn):
        raise AssertionError("a timer dies with a short-lived process")

    writer = ClipWriter(Board(7), schedule=no_timer, clear_later=handed.append)
    assert writer.write("秘密", allow_plain=False).ok and handed == [8]


def test_spawn_starts_a_detached_clearer_with_only_the_count():
    calls: list[tuple[list[str], dict]] = []
    clip_clear.spawn(42, popen=lambda argv, **kw: calls.append((argv, kw)))
    ((argv, kw),) = calls
    assert argv == [sys.executable, "-I", "-m", "cpad.dest.clip_clear", "42"]  # -I: never a cpad/ in the cwd
    assert kw["start_new_session"] and kw["close_fds"] and kw["cwd"] == "/"
    assert kw["stdin"] == kw["stdout"] == kw["stderr"] == subprocess.DEVNULL


def test_spawn_failing_is_not_a_failed_send():
    def broken(*_a, **_k):
        raise OSError("no python")

    clip_clear.spawn(1, popen=broken)


@pytest.mark.parametrize(("count", "cleared"), [(5, 1), (6, 0)])
def test_the_clearer_waits_then_clears_only_if_nothing_was_copied_since(count, cleared):
    board, slept = Board(5), []
    assert clip_clear.main([str(count)], sleep=slept.append, board=board) == 0
    assert slept == [CLEAR_AFTER_S] and board.cleared == cleared


@pytest.mark.parametrize("argv", [[], ["x"], ["-1"], ["1", "2"]])
def test_the_clearer_refuses_anything_but_one_count(argv):
    assert clip_clear.main(argv, sleep=lambda _s: None, board=Board()) == 2


def test_the_clearer_survives_a_pasteboard_it_cannot_reach():
    class Broken(Board):
        def change_count(self) -> int:
            raise OSError("no AppKit")

    assert clip_clear.main(["1"], sleep=lambda _s: None, board=Broken()) == 0
