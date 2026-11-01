"""The device's 256-entry palette (docs/19 §2.2-§2.3) from tokens.py and Mando's colours:
token entries, the four magenta sentinels, Mando's 15 colours, a dimmed copy of each, one
baked 50 % edge entry per AA_PAIRS pair, the big digits' two ramps, the lock glow's six steps,
the big REC dot's breathing steps, and the lookup tables kTokenIndex, kAa, kAaExact, kDim,
kGlowQ, kBreathDot and kBreathEdge. Every entry nobody uses
stays magenta, so a wrong index shows.
"""

from __future__ import annotations

import tokens as tk

Rgb = tuple[int, int, int]

# 4 x 4 ordered dither for the glow, [y % 4][x % 4] (lib/theme/theme_raster.cpp has the same).
BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def rgb(v: int) -> Rgb:
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def pack(c: Rgb) -> int:
    return (c[0] << 16) | (c[1] << 8) | c[2]


def mix(a: Rgb, b: Rgb, t: float) -> Rgb:
    """a * (1 - t) + b * t per channel, rounded (the mockups' blend)."""
    return tuple(round(x * (1 - t) + y * t) for x, y in zip(a, b))


def dist(a: Rgb, b: Rgb) -> int:
    return sum((x - y) ** 2 for x, y in zip(a, b))


def nearest(c: Rgb, pal: list[Rgb | None], indices: list[int]) -> int:
    return min(indices, key=lambda i: (dist(c, pal[i]), i))


class Palette:
    """The palette being laid out: pal[i] is an RGB tuple or None (unused, magenta)."""

    def __init__(self, mando: list[Rgb]) -> None:
        self.pal: list[Rgb | None] = [None] * 256
        self.mando = mando
        self.aa_entry: dict[frozenset, int] = {}
        bg = rgb(tk.token_rgb("Bg"))
        for t, (_, v) in enumerate(tk.TOKENS):
            self.pal[tk.token_index(t)] = rgb(v)
            self.pal[tk.TOKEN_DIM_BASE + t] = mix(rgb(v), bg, tk.DIM_T)
        self.pal[tk.REC_DOT_EDGE] = mix(rgb(tk.token_rgb("RecDot")), bg, tk.AA_T)
        for k, c in enumerate(mando):
            self.pal[tk.MANDO_BASE + k] = c
            self.pal[tk.MANDO_DIM_BASE + k] = mix(c, bg, tk.DIM_T)
        self._add_aa_entries()
        self._add_ramps()
        self._add_breath()

    def _add_ramps(self) -> None:
        top = tk.DIGIT_LEVELS - 1
        for ink, under, base in tk.DIGIT_RAMPS:
            for k in range(1, top):
                self.pal[base + k - 1] = mix(rgb(tk.token_rgb(under)), rgb(tk.token_rgb(ink)), k / top)
        bg = rgb(tk.token_rgb("Bg"))
        for k in range(1, tk.GLOW_STEPS + 1):
            self.pal[tk.GLOW_BASE + k - 1] = mix(bg, rgb(tk.GLOW_RGB), k / tk.GLOW_STEPS)

    def _add_breath(self) -> None:
        """Steps 1..: each distinct colour gets a dot entry from BREATH_BASE, then an edge entry
        (the colour half way to Bg) after all the dot entries. Step 0 is RecDot itself."""
        bg = rgb(tk.token_rgb("Bg"))
        distinct = list(dict.fromkeys(tk.BREATH[1:]))
        for k, c in enumerate(distinct):
            self.pal[tk.BREATH_BASE + k] = rgb(c)
            self.pal[tk.BREATH_BASE + len(distinct) + k] = mix(rgb(c), bg, tk.AA_T)
        dot0 = tk.token_index(tk.token_names().index("RecDot"))
        self.breath_dot = [dot0] + [tk.BREATH_BASE + distinct.index(c) for c in tk.BREATH[1:]]
        edge0 = tk.BREATH_BASE + len(distinct)
        self.breath_edge = [tk.REC_DOT_EDGE] + [edge0 + distinct.index(c) for c in tk.BREATH[1:]]

    def breath_entries(self) -> tuple[list[int], list[int]]:
        """(dot entries, edge entries) of every breath step."""
        return self.breath_dot, self.breath_edge

    def ramp_entries(self) -> list[int]:
        out = [base + k for _, _, base in tk.DIGIT_RAMPS for k in range(tk.DIGIT_LEVELS - 2)]
        return out + [tk.GLOW_BASE + k for k in range(tk.GLOW_STEPS)]

    def _add_aa_entries(self) -> None:
        nxt = tk.AA_FIRST
        by_colour: dict[Rgb, int] = {}
        for paint, under in tk.AA_PAIRS:
            c = mix(rgb(tk.token_rgb(paint)), rgb(tk.token_rgb(under)), tk.AA_T)
            if c not in by_colour:
                if nxt > tk.AA_LAST:
                    raise ValueError("too many edge entries")
                self.pal[nxt] = c
                by_colour[c] = nxt
                nxt += 1
            self.aa_entry[frozenset((paint, under))] = by_colour[c]

    def token_entries(self) -> list[int]:
        return [tk.token_index(t) for t in range(len(tk.TOKENS))]

    def aa_table(self) -> list[list[int]]:
        names = tk.token_names()
        # RecDotEdge breathes with the big dot: only (RecDot, Bg) is drawn with it.
        pool = self.token_entries() + sorted(set(self.aa_entry.values()))
        out = []
        for p in names:
            row = []
            for u in names:
                row.append(self.aa_of(p, u, pool))
            out.append(row)
        return out

    def aa_of(self, paint: str, under: str, pool: list[int]) -> int:
        if paint == under:
            return tk.token_index(tk.token_names().index(paint))
        if {paint, under} == {"RecDot", "Bg"}:
            return tk.REC_DOT_EDGE
        exact = self.aa_entry.get(frozenset((paint, under)))
        if exact is not None:
            return exact
        return nearest(mix(rgb(tk.token_rgb(paint)), rgb(tk.token_rgb(under)), tk.AA_T), self.pal, pool)

    def aa_exact(self) -> list[int]:
        names = tk.token_names()
        masks = []
        for p in names:
            m = 0
            for u, under in enumerate(names):
                if p == under or frozenset((p, under)) in self.aa_entry or {p, under} == {"RecDot", "Bg"}:
                    m |= 1 << u
            masks.append(m)
        return masks

    def dim_table(self) -> list[int]:
        """kDim: every entry's dimmed copy; dim entries, Bg, sentinels and unused ones stay."""
        bg = rgb(tk.token_rgb("Bg"))
        out = list(range(256))
        dims = [tk.TOKEN_DIM_BASE + t for t in range(len(tk.TOKENS))]
        dims += [tk.MANDO_DIM_BASE + k for k in range(len(self.mando))]
        for t in range(1, len(tk.TOKENS)):
            out[tk.token_index(t)] = tk.TOKEN_DIM_BASE + t
        for k in range(len(self.mando)):
            out[tk.MANDO_BASE + k] = tk.MANDO_DIM_BASE + k
        breath = sorted(set(self.breath_dot[1:] + self.breath_edge[1:]))
        for i in sorted(set(self.aa_entry.values())) + [tk.REC_DOT_EDGE] + self.ramp_entries() + breath:
            out[i] = nearest(mix(self.pal[i], bg, tk.DIM_T), self.pal, dims + [tk.token_index(0)])
        return out

    def entries888(self) -> list[int]:
        return [tk.MAGENTA if c is None else pack(c) for c in self.pal]


def glow_thresholds() -> list[int]:
    """kGlowQ[j], j = 16 (step - 1) + dither: a pixel at (dx, dy) from the glow's centre is lit
    to at least that step when q = 16 dx^2 + 25 dy^2 < kGlowQ[j]. That is the mockup's
    t = (1 - d / radius) ** falloff * steps with 16 t > j + 0.5, done without a root or a power
    (16 dx^2 + 25 dy^2 is 16 (dx^2 + (1.25 dy)^2))."""
    r = tk.GLOW_RADIUS

    def lit(q: int, j: int) -> bool:
        d = (q / 16) ** 0.5 / r
        return max(0.0, 1.0 - d) ** tk.GLOW_FALLOFF * tk.GLOW_STEPS * 16 > j + 0.5

    out = []
    for j in range(16 * tk.GLOW_STEPS):
        lo, hi = 0, 16 * r * r + 1  # the smallest q that is not lit is in [lo, hi]
        while lo < hi:
            mid = (lo + hi) // 2
            if lit(mid, j):
                lo = mid + 1
            else:
                hi = mid
        out.append(lo)
    return out


def _rows(values: list[int], fmt: str, per: int) -> list[str]:
    return ["    " + " ".join(fmt.format(v) + "," for v in values[i : i + per]) for i in range(0, len(values), per)]


def _ramp_lines() -> list[str]:
    return [
        "// The big digits: levels 1-4 of Fg on Bg and on Sheet, four entries from each base.",
        *[f"constexpr uint8_t kDigitRamp{under} = 0x{base:02X};" for _, under, base in tk.DIGIT_RAMPS],
        "",
        "// The lock glow: steps 1-6 at kGlowBase.., centred on (kGlowCx, kGlowCy). A pixel (dx, dy)",
        "// from the centre is lit to at least step s when 16 dx^2 + 25 dy^2 < kGlowQ[16 (s - 1) + d],",
        "// d the 4 x 4 ordered dither at the pixel.",
        f"constexpr uint8_t kGlowBase = 0x{tk.GLOW_BASE:02X};",
        f"constexpr uint8_t kGlowSteps = {tk.GLOW_STEPS};",
        f"constexpr int16_t kGlowCx = {tk.GLOW_CENTRE[0]};",
        f"constexpr int16_t kGlowCy = {tk.GLOW_CENTRE[1]};",
        "constexpr uint32_t kGlowQ[16 * kGlowSteps] = {",
        *_rows(glow_thresholds(), "{}", 12),
        "};",
        "",
    ]


def _breath_lines(p: Palette) -> list[str]:
    dots, edges = p.breath_entries()
    return [
        "// The big REC dot breathing (docs/19 §2.3): at step s the RecDot entry shows",
        "// kPalette888[kBreathDot[s]] and RecDotEdge (kRecDotEdge, its rim) kPalette888[kBreathEdge[s]].",
        "// The device copies them in (setPaletteColor); step 0 is the entries' own colours.",
        f"constexpr uint8_t kBreathSteps = {tk.BREATH_STEPS};",
        f"constexpr uint8_t kRecDotEdge = 0x{tk.REC_DOT_EDGE:02X};",
        "constexpr uint8_t kBreathDot[kBreathSteps] = {",
        *_rows(dots, "0x{:02X}", 8),
        "};",
        "constexpr uint8_t kBreathEdge[kBreathSteps] = {",
        *_rows(edges, "0x{:02X}", 8),
        "};",
        "",
    ]


def header(p: Palette) -> str:
    names = tk.token_names()
    lines = [
        "// Generated by tools/theme/gen_all.py from tools/theme/tokens.py and mando_colours.json",
        "// (docs/19-visual-theme.md §2). Do not edit; regenerate. Colours: WhisperWoof (MIT, NOTICE).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace cpa {",
        "namespace theme {",
        "namespace gen {",
        "",
        f"constexpr uint8_t kTokenCount = {len(names)};",
        f"constexpr uint8_t kMandoCount = {len(p.mando)};",
        "",
        "// cpa::Color value -> palette entry.",
        "constexpr uint8_t kTokenIndex[kTokenCount] = {",
        *_rows(p.token_entries(), "0x{:02X}", 12),
        "};",
        "",
        "// RGB888 of every entry; unused ones and the sentinels are magenta.",
        "constexpr uint32_t kPalette888[256] = {",
        *_rows(p.entries888(), "0x{:06X}", 8),
        "};",
        "",
        "// kAa[paint][under]: the entry of one baked edge step between two tokens.",
        "constexpr uint8_t kAa[kTokenCount][kTokenCount] = {",
        *["    {" + ", ".join(f"0x{v:02X}" for v in r) + "}," for r in p.aa_table()],
        "};",
        "",
        "// Bit u of kAaExact[paint]: (paint, under u) has an entry of its own (tokens.AA_PAIRS).",
        "constexpr uint32_t kAaExact[kTokenCount] = {",
        *_rows(p.aa_exact(), "0x{:06X}", 8),
        "};",
        "",
        "// kDim[i]: the entry that shows entry i dimmed (under the Tab sheet).",
        "constexpr uint8_t kDim[256] = {",
        *_rows(p.dim_table(), "0x{:02X}", 16),
        "};",
        "",
        *_ramp_lines(),
        *_breath_lines(p),
        "}  // namespace gen",
        "}  // namespace theme",
        "}  // namespace cpa",
        "",
    ]
    return "\n".join(lines)
