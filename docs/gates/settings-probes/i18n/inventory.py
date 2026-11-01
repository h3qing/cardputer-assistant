"""Inventory string literals in firmware C++ sources.

Usage: python3 -I inventory.py <repo_root> <out.json>
Classifies each literal: cjk (has CJK), sym (non-ASCII, no CJK), ascii. Runs of \\xHH escapes
that spell valid UTF-8 (hint_layout.cpp writes 退格 and 空格 that way) are decoded first.
Skips comments and the generated lib/theme/gen/. docs/21 §2.2: 373 CJK literals, 35 files.
"""
import json, os, re, sys

ROOT = sys.argv[1]
OUT = sys.argv[2]
DIRS = ["firmware/app/lib", "firmware/app/modules", "firmware/app/src"]

def strip_and_extract(src):
    """Return list of (line, literal_text) with comments skipped."""
    out = []
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; i += 1; continue
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); seg = src[i:j + 2]; line += seg.count("\n"); i = j + 2; continue
        if c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1; continue
        if c == 'R' and src.startswith('R"', i):
            m = re.match(r'R"([^(]*)\(', src[i:])
            d = m.group(1); end = src.find(")" + d + '"', i)
            lit = src[i + len(m.group(0)):end]
            out.append((line, lit)); line += src[i:end].count("\n"); i = end + len(d) + 2; continue
        if c == '"':
            j = i + 1; buf = []
            while j < n and src[j] != '"':
                if src[j] == "\\":
                    buf.append(src[j:j + 2]); j += 2
                else:
                    buf.append(src[j]); j += 1
            out.append((line, "".join(buf))); i = j + 1; continue
        i += 1
    return out

def is_cjk(ch):
    o = ord(ch)
    return 0x3000 <= o <= 0x9FFF or 0xFF00 <= o <= 0xFFEF or 0x3400 <= o <= 0x4DBF

def unescape_hex(s):
    def utf8(m):
        raw = bytes.fromhex(m.group(0).replace("\\x", ""))
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return m.group(0)
    return re.sub(r"(?:\\x[0-9A-Fa-f]{2})+", utf8, s)

def classify(s):
    if any(is_cjk(c) for c in s):
        return "cjk"
    if any(ord(c) > 127 for c in s):
        return "sym"
    return "ascii"

rows = []
for d in DIRS:
    for dp, _, fs in os.walk(os.path.join(ROOT, d)):
        for f in sorted(fs):
            if not f.endswith((".cpp", ".h")):
                continue
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, ROOT)
            if "/theme/gen/" in rel:
                continue
            src = open(p, encoding="utf-8").read()
            for ln, lit in strip_and_extract(src):
                text = unescape_hex(lit)
                rows.append({"file": rel, "line": ln, "text": text, "cls": classify(text)})
json.dump(rows, open(OUT, "w"), ensure_ascii=False, indent=0)
print(len(rows))
