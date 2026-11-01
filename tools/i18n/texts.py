"""docs/21 §9.3: every zh/en pair in the firmware, measured, as a TSV for reviewing English.

One row per pair: file, constant, width class, its limit, both halves and their widths in
efontCN_12 px (ASCII and ● … ★ ↑ ← → ◎ ◇ ○ · × 6 px, all else 12: txt_rules.h's table,
which test_i18n_font checks against the real glyphs). A % string has no limit here (its
test measures it with the widest arguments). Hint strings bracket their keys, so their
width counts the brackets. Exits 1 when a pair is over its limit, which the build itself
refuses, so this is the reviewer's view more than a gate.

Usage: python3 texts.py [firmware/app] [--out texts.tsv]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from check_texts import constant_statement, sources
from cppscan import literals

NARROW = set("●…★↑←→◎◇○·×")
LIMITS = {"ToastTxt": 224, "LineTxt": 232, "MenuTxt": 120, "TileTxt": 60, "SignTxt": 48, "KeyTxt": 43}
DECL = re.compile(r"constexpr\s+(\w*Txt)\s+(\w+)")
ESCAPE = re.compile(r"\\x([0-9A-Fa-f]{2})")


def unescape(text: str) -> str:
    """A C literal's text as the string it is: \\xNN escapes are UTF-8 bytes."""
    out = bytearray()
    for i, part in enumerate(ESCAPE.split(text)):
        out += bytes([int(part, 16)]) if i % 2 else part.replace('\\"', '"').replace("\\\\", "\\").encode()
    return out.decode("utf-8", errors="replace")


def width(text: str) -> int:
    return sum(6 if ord(c) < 0x80 or c in NARROW else 12 for c in text)


INLINE = re.compile(r"(\w*Txt)\s*\{\s*$")
NAME = re.compile(r"constexpr\s+[\w:<>]+\s+(\w+)")


def is_pair(a, b) -> bool:
    """a opens a brace ({"zh", "en"} or Txt{"zh", "en"}) and b follows it after a comma."""
    if a.statement != b.statement:
        return False
    return a.statement[: a.at].rstrip().endswith("{") and a.statement[a.at + 2 : b.at].strip() == ","


def pairs(rel: str, src: str) -> list[dict]:
    """The Txt pairs of src: {"zh", "en"} in a constexpr Txt statement."""
    out = []
    lits = [lit for lit in literals(src) if constant_statement(lit.statement, lit.at)]
    i = 0
    while i + 1 < len(lits):
        a, b = lits[i], lits[i + 1]
        if not is_pair(a, b):
            i += 1
            continue
        decl = a.statement[a.statement.rfind("constexpr") :]
        own = INLINE.search(a.statement[: a.at])  # MenuTxt{"..", ".."} inside a table
        m = own or DECL.search(decl)
        kind = m.group(1) if m else "Txt"
        n = NAME.search(decl)
        zh, en = unescape(a.text), unescape(b.text)
        limit = None if "%" in zh + en else LIMITS.get(kind)
        out.append({"file": rel, "name": n.group(1) if n else "?", "kind": kind, "limit": limit, "zh": zh, "en": en})
        i += 2
    return out


def report(root: Path) -> list[dict]:
    rows = []
    for p in sources(root):
        rows.extend(pairs(p.relative_to(root).as_posix(), p.read_text(encoding="utf-8")))
    for r in rows:
        r["zh_px"], r["en_px"] = width(r["zh"]), width(r["en"])
        r["over"] = r["limit"] is not None and max(r["zh_px"], r["en_px"]) > r["limit"]
    return rows


def tsv(rows: list[dict]) -> str:
    head = "file\tconstant\tclass\tlimit\tzh\tzh_px\ten\ten_px\tover"
    lines = [head]
    for r in rows:
        limit = "" if r["limit"] is None else str(r["limit"])
        lines.append(
            f"{r['file']}\t{r['name']}\t{r['kind']}\t{limit}\t{r['zh']}\t{r['zh_px']}\t{r['en']}\t{r['en_px']}\t"
            f"{'OVER' if r['over'] else ''}"
        )
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default=str(Path(__file__).resolve().parents[2] / "firmware" / "app"))
    ap.add_argument("--out")
    a = ap.parse_args(argv[1:])
    rows = report(Path(a.root))
    text = tsv(rows)
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    wider = sum(1 for r in rows if r["en_px"] > r["zh_px"])
    over = [r for r in rows if r["over"]]
    print(f"texts: {len(rows)} pairs, English wider in {wider}, over the limit: {len(over)}")
    for r in over:
        print(f"  OVER {r['file']} {r['name']} ({r['kind']} {r['limit']}): {r['zh_px']} / {r['en_px']} px")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
