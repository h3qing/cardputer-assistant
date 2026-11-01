"""Mando's sprites and the big digits (docs/19 §5.1, §7.1): 4-bit packing, the dozing lids,
the key poses, digit levels, and that the committed headers are well formed. The tests that
regenerate from WhisperWoof run only with a checkout named by CPA_WHISPERWOOF and the Pillow
the committed outputs were made with (mando_colours.json "pillow"); CI has neither and checks
the committed headers as they are."""

from __future__ import annotations

import importlib.util
import json
import os
import re
from pathlib import Path

import pytest

import gen_all
import gen_digits
import gen_mando as gm

HERE = Path(__file__).resolve().parent.parent


def pillow_version() -> str | None:
    """The installed Pillow's version, or None without it."""
    if importlib.util.find_spec("PIL") is None:
        return None
    import PIL

    return PIL.__version__


def whisperwoof_skip_reason() -> str | None:
    """Why the regenerating tests cannot run here, or None when they can."""
    root = os.environ.get("CPA_WHISPERWOOF")
    if not root or not gm.sprite_path(Path(root), "wait").exists():
        return "no WhisperWoof checkout (set CPA_WHISPERWOOF)"
    want = json.loads((HERE / "mando_colours.json").read_text(encoding="utf-8"))["pillow"]
    have = pillow_version()
    if have != want:
        return f"needs Pillow {want} (uv run --with pillow=={want}), found {have}"
    return None


WHISPERWOOF = Path(os.environ.get("CPA_WHISPERWOOF", ""))
SKIP_WHY = whisperwoof_skip_reason()
needs_whisperwoof = pytest.mark.skipif(SKIP_WHY is not None, reason=SKIP_WHY or "")


def numbers(text: str, name: str) -> list[int]:
    """The integers in the C array `name` of a generated header."""
    m = re.search(rf"{name}\[[^=]*= \{{(.*?)\n\}};", text, re.S)
    assert m, name
    body = re.sub(r"//[^\n]*", "", m.group(1))
    return [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", body)]


def constant(text: str, name: str) -> int:
    m = re.search(rf"constexpr \w+ {name} = (0x[0-9A-Fa-f]+|-?\d+);", text)
    assert m, name
    return int(m.group(1), 0)


def test_pack_is_high_nibble_first_and_round_trips():
    rows = [[1, 2, 3], [15, 0, 7]]
    data = gm.pack4(rows)
    assert data == bytes([0x12, 0x3F, 0x07])
    assert gm.unpack4(data, 3, 2) == rows
    odd = [[5, 6, 7]]
    assert gm.pack4(odd) == bytes([0x56, 0x70])
    assert gm.unpack4(gm.pack4(odd), 3, 1) == odd
    with pytest.raises(ValueError):
        gm.pack4([[16]])


def test_dozing_closes_the_eyes_of_a_copy():
    frame = [[1] * 26 for _ in range(46)]
    doze = gm.dozing(frame)
    assert frame == [[1] * 26 for _ in range(46)]  # the input is left as it was
    changed = {(x, y) for y in range(46) for x in range(26) if doze[y][x] != 1}
    assert changed == {(6, 10), (7, 10), (12, 10), (13, 10), (6, 9), (7, 9), (12, 9), (13, 9), (7, 11), (12, 11)}
    assert doze[10][6] == 14 and doze[9][6] == 5 and doze[11][7] == 9


def test_key_poses_are_the_documented_frames():
    names46 = [n for n, _ in gm.key_names(46)]
    assert len(names46) == 17 and names46[0] == "wait 0" and names46[-1] == "doze"
    assert names46[12:16] == ["hop 2", "hop 6", "hop 10", "hop 14"]
    names64 = [n for n, _ in gm.key_names(64)]
    assert len(names64) == 12 and names64[0] == "review 0" and "hop 2" not in names64


def test_ids_follow_the_key_poses():
    text = gm.ids_header([82, 12, 302, 404])
    assert constant(text, "kMando46W") == 26 and constant(text, "kMando46H") == 46
    assert constant(text, "kMando64W") == 36 and constant(text, "kMando64H") == 64
    assert constant(text, "kMando46Bytes") == 598 and constant(text, "kMando64Bytes") == 1152
    assert constant(text, "kM46Hop") == 12 and constant(text, "kM46Doze") == 16
    assert constant(text, "kM64Think") == 8 and constant(text, "kMando64Frames") == 12


def test_digit_levels_round_like_the_mockup():
    assert [gen_digits.level_of(v) for v in (0, 25, 26, 76, 77, 229, 230, 255)] == [0, 0, 1, 1, 2, 4, 5, 5]


def test_committed_sprites_are_whole_frames_of_15_colours():
    ids = (gen_all.GEN / "art_ids.h").read_text()
    for size in (46, 64):
        text = (gen_all.GEN / f"mando{size}_gen.h").read_text()
        data = numbers(text, f"kMando{size}")
        frames, nbytes = constant(ids, f"kMando{size}Frames"), constant(ids, f"kMando{size}Bytes")
        assert len(data) == frames * nbytes
        assert all(0 <= v <= 255 for v in data)
        assert len(text.splitlines()) < 400
        w, h = constant(ids, f"kMando{size}W"), constant(ids, f"kMando{size}H")
        first = gm.unpack4(bytes(data[:nbytes]), w, h)
        assert any(v for row in first for v in row) and first[0][0] == 0  # Mando, on a clear corner


def test_committed_digits_are_cells_of_six_levels():
    text = (gen_all.GEN / "digits_gen.h").read_text()
    assert len(text.splitlines()) < 400
    for face, cell, colon, height in (("Timer", 18, 7, 21), ("Clock", 20, 9, 24)):
        assert constant(text, f"k{face}Cell") == cell
        assert constant(text, f"k{face}Colon") == colon
        assert constant(text, f"k{face}H") == height
        digits = numbers(text, f"k{face}Digits")
        assert len(digits) == 10 * ((cell * height + 1) // 2)
        levels = [v for b in digits for v in (b >> 4, b & 15)]
        assert max(levels) == 5 and all(v <= 5 for v in levels)
        assert len(numbers(text, f"k{face}ColonPx")) == (colon * height + 1) // 2


FLASH_BUDGET = 60 * 1024  # docs/19 §10: every theme asset together


def array_bytes(text: str) -> int:
    """Bytes of every const array a generated header defines (uint8_t 1, uint32_t 4)."""
    total = 0
    for m in re.finditer(r"constexpr (uint8_t|uint32_t) (k\w+)((?:\[[^\]]+\])+) = \{", text):
        width = 4 if m.group(1) == "uint32_t" else 1
        total += width * len(numbers(text, m.group(2)))
    return total


def test_assets_fit_the_flash_budget():
    sizes = {p.name: array_bytes(p.read_text()) for p in sorted(gen_all.GEN.glob("*.h"))}
    assert sizes["mando46_gen.h"] == 17 * 598 and sizes["mando64_gen.h"] == 12 * 1152
    assert sizes["digits_gen.h"] == 10 * 189 + 74 + 10 * 240 + 108
    assert sum(sizes.values()) <= FLASH_BUDGET, sizes


@needs_whisperwoof
def test_whisperwoof_outputs_are_committed():
    assert gen_all.run(WHISPERWOOF, check=True) == 0


@needs_whisperwoof
def test_digit_cells_hold_their_ink():
    faces = gen_digits.build(WHISPERWOOF)
    for f in faces:
        assert len(f.glyphs) == 11
        for ch, rows in f.glyphs.items():
            assert len(rows) == f.h
            assert all(len(r) == (f.colon if ch == ":" else f.cell) for r in rows)
            assert max(max(r) for r in rows) == 5
