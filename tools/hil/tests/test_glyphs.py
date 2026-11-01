"""Glyph coverage of the u8g2 font the firmware links (efontCN_12 from M5GFX 0.2.33)."""

import pytest

from harness.glyphs import GlyphError, c_array_bytes, find_font_source, font_codepoints, missing, u8g2_codepoints


def u8g2_font(ascii_codes, unicode_codes):
    """A minimal u8g2 font: 23-byte header, 8-bit glyphs, unicode lookup table, unicode glyphs."""
    low = b"".join(bytes([c, 3, 0xAA]) for c in ascii_codes) + b"\0\0"
    table = bytes([0, 8]) + max(unicode_codes).to_bytes(2, "big") + b"\0\0\xff\xff"
    high = b"".join(c.to_bytes(2, "big") + bytes([4, 0xBB]) for c in unicode_codes) + b"\0\0"
    header = bytearray(23)
    header[21:23] = len(low).to_bytes(2, "big")
    return bytes(header) + low + table + high


def c_source(name, data, decoy=b"\1\2"):
    def literal(blob):
        return "".join(f"\\{b:o}" for b in blob)

    half = len(data) // 2
    return (
        f'PROGMEM const uint8_t {name}_b[{len(decoy) + 1}] = "{literal(decoy)}";\n'
        f"PROGMEM const uint8_t {name}[{len(data) + 1}] = \n"
        f'  "{literal(data[:half])}"\n  "{literal(data[half:])}";\n'
    )


def test_u8g2_codepoints_lists_8bit_and_unicode_glyphs():
    font = u8g2_font([32, 65, 97], [0x4E2D, 0x6587])
    assert u8g2_codepoints(font) == frozenset({32, 65, 97, 0x4E2D, 0x6587})


def test_u8g2_codepoints_tolerates_the_missing_trailing_terminator_byte():
    font = u8g2_font([32], [0x4E2D])[:-1]
    assert u8g2_codepoints(font) == frozenset({32, 0x4E2D})


def test_u8g2_codepoints_rejects_a_truncated_font():
    with pytest.raises(GlyphError):
        u8g2_codepoints(b"\0" * 10)


def test_c_array_bytes_reads_the_named_array_only():
    data = u8g2_font([32], [0x4E2D])
    src = c_source("lgfx_efont_cn_12", data)
    assert c_array_bytes(src, "lgfx_efont_cn_12") == data
    assert c_array_bytes(src, "lgfx_efont_cn_12_b") == b"\1\2"


def test_c_array_bytes_handles_c_escapes_and_semicolons_inside_strings():
    src = 'const uint8_t f[9] = "a;\\"\\\\\\x41\\101" "\\0z";'
    assert c_array_bytes(src, "f") == b'a;"\\AA\0z'


def test_c_array_bytes_reports_a_missing_array():
    with pytest.raises(GlyphError, match="nope"):
        c_array_bytes('const uint8_t f[1] = "";', "nope")


def test_missing_lists_each_uncovered_character_once_in_order():
    cps = frozenset(map(ord, "会议录音 M5"))
    assert missing(["会议录音和转录", "M5 和"], cps) == ("和", "转")
    assert missing(["会议"], cps) == ()


def test_font_codepoints_reads_a_source_file(tmp_path):
    path = tmp_path / "lgfx_efont_cn.c"
    path.write_text(c_source("lgfx_efont_cn_12", u8g2_font([32, 65], [0x4E2D])), encoding="ascii")
    assert font_codepoints(path) == frozenset({32, 65, 0x4E2D})


def test_find_font_source_looks_in_platformio_libdeps(tmp_path):
    assert find_font_source(tmp_path) is None
    font = tmp_path / ".pio/libdeps/cardputer-adv-test/M5GFX/src/lgfx/Fonts/efont/lgfx_efont_cn.c"
    font.parent.mkdir(parents=True)
    font.write_text("", encoding="ascii")
    assert find_font_source(tmp_path) == font
