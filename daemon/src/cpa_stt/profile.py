"""The macOS sandbox profile wrapped around every transcriber command (docs/22 §4.5). Pure.

It blocks direct network access and writes anywhere but the run's own empty work directory, /dev/null,
dtrace's helper and the Metal shader cache (without it every run recompiles Metal, ~5 s), and allows only
the listed Mach services (by default the two whisper-cli needs for Metal). It does not block reads: the
command can read the user's files, but cannot send them anywhere or write them elsewhere.
"""

from __future__ import annotations

import os
import re

UNQUOTABLE = re.compile(r'["\\\x00-\x1f\x7f]')


def quote(path: str) -> str:
    if not os.path.isabs(path) or UNQUOTABLE.search(path):
        raise ValueError("sandbox paths must be absolute, without quotes, backslashes or control characters")
    return f'"{path}"'


def build(workdir: str, cache_dir: str | None, mach: tuple[str, ...]) -> str:
    writable = ['(literal "/dev/null")', '(literal "/dev/dtracehelper")']
    if cache_dir:
        writable.append(f"(subpath {quote(cache_dir.rstrip('/') + '/com.apple.metal')})")
    lines = [
        "(version 1)",
        "(allow default)",
        "(deny network*)",
        f"(deny file-write* (require-not (subpath {quote(workdir)})))",
        f"(allow file-write* {' '.join(writable)})",
        "(deny mach-lookup)",
    ]
    if mach:
        lines.append(f"(allow mach-lookup {' '.join(f'(global-name {quote_name(n)})' for n in mach)})")
    return "\n".join(lines) + "\n"


def quote_name(name: str) -> str:
    if UNQUOTABLE.search(name) or not name:
        raise ValueError("bad service name")
    return f'"{name}"'
