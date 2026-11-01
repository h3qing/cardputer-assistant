"""C++ source as statements of code and string literals, comments dropped (docs/21 §9.3).

Enough C++ for firmware sources: // and /* */ comments, "..." literals with escapes and
'...' character literals. A statement runs to the next ';' at any depth, so a constexpr
array of Txt pairs is one statement.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

HAN = re.compile(r"[　-鿿＀-￯]")
# A UTF-8 lead byte of U+3000-U+9FFF written as an escape: \xE3 .. \xE9.
HAN_ESCAPE = re.compile(r"\\x[eE][3-9]")


@dataclass(frozen=True)
class Literal:
    text: str  # as written between the quotes
    line: int
    statement: str  # the code of its statement, literals as ""
    at: int  # where in statement its "" is


def _skip_comment(src: str, i: int) -> int:
    if src.startswith("//", i):
        end = src.find("\n", i)
        return len(src) if end < 0 else end
    end = src.find("*/", i + 2)
    return len(src) if end < 0 else end + 2


def _read_quoted(src: str, i: int, quote: str) -> int:
    j = i + 1
    while j < len(src) and src[j] != quote:
        j += 2 if src[j] == "\\" else 1
    return j + 1


def literals(src: str) -> list[Literal]:
    """Every "..." literal in src with its line and the code of its statement."""
    out: list[Literal] = []
    pending: list[tuple[str, int, int]] = []
    code: list[str] = []
    i, line = 0, 1
    while i < len(src):
        c = src[i]
        if src.startswith("//", i) or src.startswith("/*", i):
            j = _skip_comment(src, i)
        elif c in "\"'":
            j = _read_quoted(src, i, c)
            if c == '"':
                pending.append((src[i + 1 : j - 1], line, sum(len(x) for x in code)))
                code.append('""')
        else:
            j = i + 1
            code.append(c)
            if c == ";":
                stmt = "".join(code)
                out.extend(Literal(t, n, stmt, at) for t, n, at in pending)
                pending, code = [], []
        line += src.count("\n", i, j)
        i = j
    stmt = "".join(code)
    out.extend(Literal(t, n, stmt, at) for t, n, at in pending)
    return out


def has_han(text: str) -> bool:
    return bool(HAN.search(text) or HAN_ESCAPE.search(text))
