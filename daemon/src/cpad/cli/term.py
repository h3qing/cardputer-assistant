"""One key at a time from a terminal, for cpa filing triage (docs/23 §3.6). I/O shell.

raw() puts the terminal in cbreak mode while it is used and gives its own settings back on the way out, also after
an error or Ctrl-C (a terminal that went away has none to give back, and leaving it is not an error):
- no line editing and no echo; output processing and signals stay on, so a newline is still a newline and Ctrl-C
  still interrupts;
- keys typed before it started are dropped (TCSAFLUSH), and drop() throws away keys typed since: the caller drops
  them just before it shows a new screen, so nothing is filed by typing ahead of it (a double press files one row).
A key is one character (UTF-8), "enter", "eof" (Ctrl-D, the end of input, or a terminal that went away), "esc" (an
Escape that nothing follows within ESC_WAIT_S), or a whole escape sequence (an arrow, a function key, Alt with a
key), which is never Escape, so an arrow key cannot quit. A full-width character a Chinese input method sends for
a key (２ ｘ ？, the ideographic space) is that key (NFKC); any other character stays itself.
"""

from __future__ import annotations

import os
import select
import termios
import tty
import unicodedata
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from typing import IO

ESC_WAIT_S = 0.05
SEQ_MAX = 16  # bytes of one escape sequence
EOF, ENTER, ESC = "eof", "enter", "esc"


def _length(lead: int) -> int:
    """Bytes of the UTF-8 character that starts with `lead`; 1 for anything that cannot start one."""
    for size, mask, value in ((2, 0xE0, 0xC0), (3, 0xF0, 0xE0), (4, 0xF8, 0xF0)):
        if lead & mask == value:
            return size
    return 1


def _final(byte: bytes) -> bool:
    """The last byte of a CSI sequence (ESC [ ... final)."""
    return b"\x40" <= byte <= b"\x7e"


@dataclass(frozen=True, slots=True)
class RawTerm:
    fd: int
    out: IO[str]

    def _read(self, n: int) -> bytes:
        try:
            return os.read(self.fd, n)
        except OSError:  # EIO: the terminal went away
            return b""

    def _more(self) -> bytes:
        """The next byte of a sequence, or nothing when none follows within ESC_WAIT_S (a cut sequence never
        blocks)."""
        return self._read(1) if select.select([self.fd], [], [], ESC_WAIT_S)[0] else b""

    def _sequence(self) -> str:
        """After ESC: Escape alone, or the whole sequence it starts."""
        seq = self._more()
        if not seq:
            return ESC
        if seq == b"[":
            while len(seq) < SEQ_MAX and (byte := self._more()):
                seq += byte
                if _final(byte):
                    break
        elif seq == b"O":
            seq += self._more()
        return "\x1b" + seq.decode("utf-8", errors="replace")

    def key(self) -> str:
        first = self._read(1)
        if first in (b"", b"\x04"):
            return EOF
        if first in (b"\r", b"\n"):
            return ENTER
        if first == b"\x1b":
            return self._sequence()
        rest = self._read(_length(first[0]) - 1) if _length(first[0]) > 1 else b""
        text = (first + rest).decode("utf-8", errors="replace")
        narrow = unicodedata.normalize("NFKC", text)
        return narrow if len(narrow) == 1 else text

    def drop(self) -> None:
        termios.tcflush(self.fd, termios.TCIFLUSH)

    def say(self, line: str) -> None:
        self.out.write(line + "\n")
        self.out.flush()


@contextmanager
def raw(inp: IO, out: IO[str]) -> Iterator[RawTerm]:
    fd = inp.fileno()
    before = termios.tcgetattr(fd)
    tty.setcbreak(fd, termios.TCSAFLUSH)
    try:
        yield RawTerm(fd, out)
    finally:
        try:
            termios.tcsetattr(fd, termios.TCSADRAIN, before)
        except termios.error:  # EIO (Linux): the terminal went away, there is nothing to give the settings back to
            pass
