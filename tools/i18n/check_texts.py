"""docs/21 §6.3, §9.3: every Chinese string the firmware draws is half of a checked Txt.

Fails on a string literal with Han (raw, or as \\xE3-\\xE9 escapes) in lib/ or modules/
whose statement is not a constexpr declaration of Txt constants (only those are checked
when built, txt.h), and on a Txt built inline (Txt{...} outside a constexpr statement,
which skips the checks). Allowed: lib/i18n/lang_names.h (each language's own name) and
the key names the hint parser matches (lib/theme/hint_layout.cpp).

Usage: python3 check_texts.py [firmware/app]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from cppscan import has_han, literals

ALLOWED = {"lib/i18n/lang_names.h", "lib/theme/hint_layout.cpp"}
TXT_TYPE = re.compile(r"\b\w*Txt\b")
INLINE_TXT = re.compile(r"\b\w*Txt\s*[{(]")


def constant_statement(stmt: str, lit_at: int | None = None) -> bool:
    """A constexpr declaration (its statement's last) whose initializer holds Txt pairs,
    with the literal at lit_at (when given) inside it."""
    at = stmt.rfind("constexpr")
    inside = lit_at is None or lit_at > at
    return at >= 0 and inside and bool(TXT_TYPE.search(stmt[at:]))


def problems_in(rel: str, src: str) -> list[str]:
    if rel in ALLOWED:
        return []
    out = []
    seen = set()
    for lit in literals(src):
        if has_han(lit.text) and not constant_statement(lit.statement, lit.at):
            out.append(f'{rel}:{lit.line}: Chinese outside a constexpr Txt: "{lit.text}"')
        stmt = lit.statement
        if INLINE_TXT.search(stmt) and not constant_statement(stmt) and stmt not in seen and has_han(lit.text):
            seen.add(stmt)
            out.append(f"{rel}:{lit.line}: a Txt built inline is never checked")
    return out


def sources(root: Path) -> list[Path]:
    files = []
    for top in ("lib", "modules"):
        for p in sorted((root / top).rglob("*")):
            if p.suffix in (".h", ".cpp") and "/gen/" not in p.as_posix():
                files.append(p)
    return files


def check(root: Path) -> list[str]:
    out = []
    for p in sources(root):
        out.extend(problems_in(p.relative_to(root).as_posix(), p.read_text(encoding="utf-8")))
    return out


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[2] / "firmware" / "app"
    found = check(root)
    for f in found:
        print(f)
    print(f"check_texts: {len(found)} problem(s) in {len(sources(root))} files")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
