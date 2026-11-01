"""Host-side line splitter (05 §6.1): split on \\n, drop \\r, cap a line at line_max bytes.

A line longer than line_max is reported once as its first line_max+1 bytes (which the codec
classifies as E_TOO_LARGE) and the rest is discarded up to the next \\n. Memory stays bounded.
"""

from __future__ import annotations

from dataclasses import dataclass

from cpad.protocol.codec import LINE_MAX


@dataclass(frozen=True, slots=True)
class Splitter:
    buf: bytes = b""
    discarding: bool = False


def _oversize(head: bytes) -> bytes:
    return head[:LINE_MAX] + b"\n"


def split(state: Splitter, chunk: bytes) -> tuple[Splitter, tuple[bytes, ...]]:
    *complete, rest = (state.buf + chunk.replace(b"\r", b"")).split(b"\n")
    lines = tuple(
        _oversize(line + b"\n") if index == 0 and state.discarding else line + b"\n"
        for index, line in enumerate(complete)
    )
    discarding = state.discarding and not complete
    if len(rest) >= LINE_MAX:
        return Splitter(rest[:LINE_MAX], True), lines
    return Splitter(rest, discarding), lines
