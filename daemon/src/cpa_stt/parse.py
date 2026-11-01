"""The transcriber command's stdout (docs/22 §4.3). Pure.

UTF-8, one sentence per line; a line may start with "[HH:MM:SS.mmm --> HH:MM:SS.mmm]", whose start is the
sentence's t_ms (from the start of the WAV it was given); without one t_ms is 0. Empty output = nothing
heard. Control characters become spaces and a sentence is cut to the contract's 10000 characters. How much
stdout is read is the runner's cap (runner.stdout_cap), not cut here.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

STDOUT_MAX = 256 * 1024  # the stdout cap of a run of no length; it grows with the recording (runner)
TEXT_MAX = 10000
STAMP = re.compile(r"^\[(\d{1,3}):(\d{2}):(\d{2})[.,](\d{3}) --> [0-9:.,]+\]\s*")
CONTROL = re.compile(r"[\x00-\x1f\x7f]")


@dataclass(frozen=True, slots=True)
class Sentence:
    t_ms: int
    text: str


def _line(line: str) -> Sentence | None:
    found = STAMP.match(line)
    t_ms = 0
    if found:
        h, m, s, ms = (int(g) for g in found.groups())
        t_ms, line = ((h * 60 + m) * 60 + s) * 1000 + ms, line[found.end() :]
    text = " ".join(CONTROL.sub(" ", line).split())[:TEXT_MAX]
    return Sentence(t_ms, text) if text else None


def parse(data: bytes) -> tuple[Sentence, ...]:
    lines = data.decode("utf-8", errors="replace").splitlines()
    return tuple(s for s in (_line(line) for line in lines) if s is not None)
