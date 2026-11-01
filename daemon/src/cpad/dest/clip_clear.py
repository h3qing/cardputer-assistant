"""python -m cpad.dest.clip_clear COUNT: clears the clipboard CLEAR_AFTER_S from now if its changeCount is still COUNT.

A cpa process (cpa filing send, cpa dest send, cpa dest test --send) ends long before the 2 minutes ClipWriter
promises (docs/25 §5.3, D9), and a timer thread dies with it; so cpa hands the clear to this detached process.
Its argv holds only the changeCount, never text; it runs isolated (-I) from /, so no cpad/ folder in the directory
cpa was run from is imported in place of the installed one. It reads nothing from the clipboard and clears it only
if nothing was copied since. Anything else (no AppKit, a bad argument) leaves the clipboard as it is.
"""

from __future__ import annotations

import subprocess
import sys
import time
from collections.abc import Callable, Sequence
from typing import Any

from cpad.dest.pasteboard import CLEAR_AFTER_S, NativePasteboard, Pasteboard


def spawn(count: int, popen: Callable[..., Any] = subprocess.Popen) -> None:
    """Start the clearer for this changeCount; never fails a send (the copy then stays until you copy again)."""
    try:
        popen([sys.executable, "-I", "-m", "cpad.dest.clip_clear", str(count)], stdin=subprocess.DEVNULL,
              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, close_fds=True, start_new_session=True,
              cwd="/")  # fmt: skip
    except OSError:
        pass


def main(argv: Sequence[str], sleep: Callable[[float], None] = time.sleep, board: Pasteboard | None = None) -> int:
    if len(argv) != 1 or not argv[0].isdigit():
        return 2
    sleep(CLEAR_AFTER_S)
    found = board or NativePasteboard()
    try:
        if found.change_count() == int(argv[0]):
            found.clear()
    except (OSError, AttributeError, ValueError):
        pass
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main(sys.argv[1:]))
