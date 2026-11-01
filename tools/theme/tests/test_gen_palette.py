"""The palette generator (docs/19 §2.2-§2.3, §5.1): layout, sentinels, edge pairs, dimming,
the icon table, and that the committed headers are what the generator writes."""

from __future__ import annotations

import re
from pathlib import Path

import pytest

import gen_all
import gen_icons
import gen_mando
import gen_palette as gp
import tokens as tk

MANDO = gen_mando.load_colours(gen_all.COLOURS)


@pytest.fixture(scope="module")
def pal() -> gp.Palette:
    return gp.Palette(MANDO)


def test_tokens_sit_around_the_sentinels():
    entries = [tk.token_index(t) for t in range(len(tk.TOKENS))]
    assert entries[0] == 0x01 and entries[14] == 0x0F and entries[15] == 0x11
    assert len(set(entries)) == len(entries) == 24
    assert not set(entries) & set(tk.SENTINELS)


def test_sentinels_are_magenta_and_catch_the_old_constants(pal):
    e = pal.entries888()
    for s in tk.SENTINELS:
        assert e[s] == tk.MAGENTA
    for rgb565 in (0x0000, 0xFFFF, 0x8410, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0x0010):
        assert rgb565 & 0xFF in tk.SENTINELS


def test_every_token_has_its_colour(pal):
    e = pal.entries888()
    for t, (_, v) in enumerate(tk.TOKENS):
        assert e[tk.token_index(t)] == v


def test_nothing_used_is_magenta_and_it_fits(pal):
    used = [i for i, c in enumerate(pal.pal) if c is not None]
    assert len(used) <= 256 - len(tk.SENTINELS)
    assert all(pal.pal[i] != gp.rgb(tk.MAGENTA) for i in used)
    assert not set(used) & set(tk.SENTINELS)


def test_listed_pairs_get_an_exact_edge_both_ways(pal):
    table = pal.aa_table()
    names = tk.token_names()
    masks = pal.aa_exact()
    for paint, under in tk.AA_PAIRS:
        p, u = names.index(paint), names.index(under)
        want = gp.mix(gp.rgb(tk.token_rgb(paint)), gp.rgb(tk.token_rgb(under)), 0.5)
        assert pal.pal[table[p][u]] == want
        assert table[p][u] == table[u][p]
        assert masks[p] >> u & 1 and masks[u] >> p & 1


def test_other_pairs_fall_back_to_a_real_entry(pal):
    for row in pal.aa_table():
        for i in row:
            assert i not in tk.SENTINELS and pal.pal[i] is not None
    names = tk.token_names()
    assert pal.aa_table()[names.index("RecDot")][names.index("Bg")] == tk.REC_DOT_EDGE
    assert pal.aa_table()[names.index("Ok")][names.index("Ok")] == tk.token_index(names.index("Ok"))


def lum(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    return 0.2126 * ch(c[0]) + 0.7152 * ch(c[1]) + 0.0722 * ch(c[2])


def test_dimming_never_brightens_and_keeps_bg(pal):
    dim = pal.dim_table()
    assert dim[tk.token_index(0)] == tk.token_index(0)
    for i, c in enumerate(pal.pal):
        if c is None:
            assert dim[i] == i
            continue
        assert pal.pal[dim[i]] is not None
        assert lum(pal.pal[dim[i]]) <= lum(c) + 1e-9


def test_icon_uses_mando_and_token_entries():
    px = gen_icons.pixels(gen_icons.HEAD, gen_icons.HEAD_KEY)
    assert len(px) == 11 * 9
    for v in px:
        assert v == 0 or tk.MANDO_BASE <= v < tk.MANDO_BASE + 15 or v == tk.token_index(17)
    with pytest.raises(ValueError):
        gen_icons.entry_of("m16")
    with pytest.raises(ValueError):
        gen_icons.pixels(["..", "."], {})


def test_generation_is_stable():
    a = gen_all.outputs(None)
    b = gen_all.outputs(None)
    assert a == b
    assert re.search(r"kPalette888\[256\]", a[gen_all.GEN / "palette_gen.h"])


def test_committed_headers_are_current():
    assert gen_all.run(None, check=True) == 0


def test_check_reports_a_stale_file(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(gen_all, "GEN", tmp_path / "gen")
    monkeypatch.setattr(gen_all, "REPO", tmp_path)
    assert gen_all.run(None, check=True) == 1
    assert "differs: gen/palette_gen.h" in capsys.readouterr().out
    assert gen_all.run(None, check=False) == 0
    assert gen_all.run(None, check=True) == 0
    (tmp_path / "gen" / "icons_gen.h").write_text("// edited\n")
    assert gen_all.main(["--palette-only", "--check"]) == 1


def test_colours_file_round_trips(tmp_path):
    data = {"colours": ["#010203", "#ffeedd"], "box": [0, 0, 1, 1]}
    p = Path(tmp_path / "c.json")
    p.write_text(gen_mando.render(data))
    assert gen_mando.load_colours(p) == [(1, 2, 3), (255, 238, 221)]
    assert len(MANDO) == 15


# --- S2: the digits' ramps and the lock screen's glow (docs/19 §2.3) ---


def test_digit_ramps_are_four_steps_from_the_ground_to_the_ink(pal):
    for ink, under, base in tk.DIGIT_RAMPS:
        for k in range(1, tk.DIGIT_LEVELS - 1):
            want = gp.mix(gp.rgb(tk.token_rgb(under)), gp.rgb(tk.token_rgb(ink)), k / (tk.DIGIT_LEVELS - 1))
            assert pal.pal[base + k - 1] == want
        assert base + tk.DIGIT_LEVELS - 2 <= tk.GLOW_BASE or base >= tk.GLOW_BASE + tk.GLOW_STEPS


def test_glow_ramp_runs_from_bg_to_the_glow(pal):
    bg = gp.rgb(tk.token_rgb("Bg"))
    for k in range(1, tk.GLOW_STEPS + 1):
        assert pal.pal[tk.GLOW_BASE + k - 1] == gp.mix(bg, gp.rgb(tk.GLOW_RGB), k / tk.GLOW_STEPS)


def test_ramps_dim_to_a_real_entry(pal):
    dim = pal.dim_table()
    for i in list(range(0xA0, 0xA8)) + list(range(tk.GLOW_BASE, tk.GLOW_BASE + tk.GLOW_STEPS)):
        assert pal.pal[dim[i]] is not None and dim[i] not in tk.SENTINELS
        assert lum(pal.pal[dim[i]]) <= lum(pal.pal[i]) + 1e-9


def mockup_glow(x, y):
    """scratchpad/visual/companion/src/s_lock.py glow(): the step drawn at pixel (x, y)."""
    cx, cy = tk.GLOW_CENTRE
    d = ((x - cx) ** 2 + ((y - cy) * 1.25) ** 2) ** 0.5 / tk.GLOW_RADIUS
    t = max(0.0, 1.0 - d) ** tk.GLOW_FALLOFF * tk.GLOW_STEPS
    k = int(t)
    if (t - k) * 16 > gp.BAYER4[y % 4][x % 4] + 0.5:
        k += 1
    return min(k, tk.GLOW_STEPS)


def table_glow(q_table, x, y):
    """What lib/theme/theme_raster.cpp does with the table: no root, no power."""
    cx, cy = tk.GLOW_CENTRE
    q = 16 * (x - cx) ** 2 + 25 * (y - cy) ** 2
    b = gp.BAYER4[y % 4][x % 4]
    k = 0
    while k < tk.GLOW_STEPS and q < q_table[16 * k + b]:
        k += 1
    return k


def test_glow_table_falls_with_distance():
    q = gp.glow_thresholds()
    assert len(q) == 16 * tk.GLOW_STEPS
    assert all(a >= b for a, b in zip(q, q[1:]))
    assert q[0] <= 16 * tk.GLOW_RADIUS**2 + 1 and q[-1] >= 0


def test_glow_table_draws_the_mockup_glow():
    q = gp.glow_thresholds()
    diff = sum(table_glow(q, x, y) != mockup_glow(x, y) for y in range(135) for x in range(240))
    assert diff == 0
    assert table_glow(q, 0, 0) >= 4 and table_glow(q, 239, 134) == 0


# --- S3: the breathing REC dot (docs/19 §2.3) ---


def shown(c):
    """What the panel shows of an RGB888 colour: RGB565, widened back to 8 bits."""
    r5, g6, b5 = c[0] >> 3, c[1] >> 2, c[2] >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def contrast(a, b):
    la, lb = lum(shown(a)), lum(shown(b))
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def test_breath_steps_are_the_dot_and_never_dark(pal):
    bg = gp.rgb(tk.token_rgb("Bg"))
    dots, edges = pal.breath_entries()
    assert len(dots) == len(edges) == tk.BREATH_STEPS == len(tk.BREATH)
    assert dots[0] == tk.token_index(tk.token_names().index("RecDot")) and edges[0] == tk.REC_DOT_EDGE
    assert tk.BREATH[0] == tk.token_rgb("Error")  # step 0 is Rec itself
    for s in range(tk.BREATH_STEPS):
        assert pal.pal[dots[s]] == gp.rgb(tk.BREATH[s])
        assert pal.pal[edges[s]] == gp.mix(gp.rgb(tk.BREATH[s]), bg, tk.AA_T)
        assert contrast(pal.pal[dots[s]], bg) >= 4.5  # the judges: never darker than 4.5:1
    assert dots[1] == dots[3] and edges[1] == edges[3]


def test_only_the_big_dot_breathes(pal):
    """The entries a breath step swaps in, and RecDotEdge, are drawn by nothing else: no other
    pair's edge falls back to them, so the timeline's Rec dot keeps its colour."""
    names = tk.token_names()
    dots, edges = pal.breath_entries()
    swapped = set(dots[1:]) | set(edges[1:])
    for p, row in zip(names, pal.aa_table()):
        for u, e in zip(names, row):
            assert e not in swapped
            if e == tk.REC_DOT_EDGE:
                assert {p, u} == {"RecDot", "Bg"}


def test_breath_entries_dim_to_a_real_entry(pal):
    dim = pal.dim_table()
    dots, edges = pal.breath_entries()
    for i in dots + edges:
        assert pal.pal[dim[i]] is not None and dim[i] not in tk.SENTINELS


def test_the_breath_table_is_in_the_header():
    h = gen_all.outputs(None)[gen_all.GEN / "palette_gen.h"]
    assert "kBreathDot[kBreathSteps] = {\n    0x19, 0xAE, 0xAF, 0xAE,\n};" in h
    assert "kBreathEdge[kBreathSteps] = {\n    0x1A, 0xB0, 0xB1, 0xB0,\n};" in h


def test_s3_shapes_have_their_own_edges():
    assert ("Accent2", "Bg") in tk.AA_PAIRS  # the memo waveform's bar ends
    assert ("Error", "Bg") in tk.AA_PAIRS  # the meeting timeline's Rec dot
