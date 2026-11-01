"""Which characters can the device draw? Coverage of the u8g2 font the firmware links (05 §5.12).

The firmware renders with M5GFX's efontCN_12 (array lgfx_efont_cn_12 in lgfx_efont_cn.c) and draws
a missing glyph as □. Reading the same table on the host lists exactly which characters of a
session name would come out as □, without a camera.

u8g2 font layout: a 23-byte header (bytes 21-22: offset of the unicode section, counted from byte
23); 8-bit glyphs [code, size, ...] up to size 0; then a lookup table of [offset16, code16] entries
(the first offset is the table's own size) and unicode glyphs [code16, size, ...] up to code 0.
"""

from __future__ import annotations

import re
from collections.abc import Iterable, Iterator
from pathlib import Path

HEADER = 23
FONT_ARRAY = "lgfx_efont_cn_12"
FONT_GLOB = ".pio/libdeps/*/M5GFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c"
LITERAL = re.compile(r'\s*"((?:[^"\\\n]|\\.)*)"')
ESCAPE = re.compile(r"\\([0-7]{1,3}|x[0-9a-fA-F]+|.)")
SIMPLE = {"n": 10, "t": 9, "r": 13, "a": 7, "b": 8, "f": 12, "v": 11, "\\": 92, '"': 34, "'": 39, "?": 63}


class GlyphError(ValueError):
    """The font source or table could not be read."""


def _unescape(text: str) -> bytes:
    out, pos = bytearray(), 0
    for m in ESCAPE.finditer(text):
        out += text[pos : m.start()].encode("ascii")
        code = m.group(1)
        if code[0] in "01234567":
            out.append(int(code, 8) & 0xFF)
        elif code[0] == "x":
            out.append(int(code[1:], 16) & 0xFF)
        else:
            out.append(SIMPLE[code])
        pos = m.end()
    return bytes(out + text[pos:].encode("ascii"))


def c_array_bytes(source: str, name: str) -> bytes:
    """The bytes of `name[N] = "..." "...";` (without the literal's implicit trailing NUL)."""
    m = re.search(rf"\b{re.escape(name)}\[\d+\]\s*=", source)
    if m is None:
        raise GlyphError(f"array {name} not found")
    parts, pos = [], m.end()
    while lit := LITERAL.match(source, pos):
        parts.append(_unescape(lit.group(1)))
        pos = lit.end()
    return b"".join(parts)


def _byte(data: bytes, i: int) -> int:
    return data[i] if i < len(data) else 0  # past the end: the C literal's NUL terminates the font


def _u16(data: bytes, i: int) -> int:
    return _byte(data, i) << 8 | _byte(data, i + 1)


def _low_glyphs(font: bytes) -> Iterator[int]:
    p = HEADER
    while _byte(font, p + 1) != 0:
        yield font[p]
        p += font[p + 1]


def _unicode_glyphs(font: bytes) -> Iterator[int]:
    table = HEADER + _u16(font, 21)
    p = table + _u16(font, table)
    while (code := _u16(font, p)) != 0:
        size = _byte(font, p + 2)
        if size == 0:
            raise GlyphError(f"glyph U+{code:04X} has size 0")
        yield code
        p += size


def u8g2_codepoints(font: bytes) -> frozenset[int]:
    if len(font) < HEADER:
        raise GlyphError("font table shorter than its header")
    return frozenset(_low_glyphs(font)) | frozenset(_unicode_glyphs(font))


def font_codepoints(path: Path, name: str = FONT_ARRAY) -> frozenset[int]:
    return u8g2_codepoints(c_array_bytes(Path(path).read_text(encoding="ascii"), name))


def missing(texts: Iterable[str], codepoints: frozenset[int]) -> tuple[str, ...]:
    """Characters the font lacks (drawn as □), each once, in first-seen order."""
    seen: dict[str, None] = {}
    for ch in (c for t in texts for c in t):
        if ord(ch) not in codepoints:
            seen.setdefault(ch, None)
    return tuple(seen)


def find_font_source(firmware_dir: Path) -> Path | None:
    return next(iter(sorted(Path(firmware_dir).glob(FONT_GLOB))), None)
