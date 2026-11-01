"""The theme's single token table (docs/19-visual-theme.md §2): colour tokens, the 256-entry
palette layout of the device's palette_8bit canvas, the sentinels, the baked edge pairs and
the dimming factor. gen_palette.py turns this into firmware/app/lib/theme/gen/palette_gen.h.

Colours are WhisperWoof's dark theme (DESIGN.md, src/index.css .dark; MIT, same author)
plus REC; ink3 is raised from #8f8276 to #96897b so it reads at 4.5:1 on a card.
"""

from __future__ import annotations

# cpa::Color in enum order (lib/module_api/canvas.h): (name, RGB888).
TOKENS: list[tuple[str, int]] = [
    ("Bg", 0x120E0B),  # page
    ("Fg", 0xF3EDE6),  # ink: body text (alias Ink)
    ("Dim", 0x96897B),  # ink3: secondary text, hints (alias Ink3)
    ("Accent", 0xE2A867),  # caramel: focus, G0, listening
    ("Ok", 0x72C291),
    ("Warn", 0xDCC15A),  # mustard: needs you
    ("Error", 0xF4675B),  # REC and errors (alias Rec)
    ("Select", 0x463525),  # selected card, live-words fill
    ("Sheet", 0x1A1512),  # lock card
    ("Card", 0x221C18),  # cards, the Tab sheet
    ("Raised", 0x2C241E),  # neutral pills
    ("Line", 0x3A2F27),  # tracks, scroll bar
    ("Cap", 0x43372D),  # keycaps
    ("Rim", 0x5A4E44),  # top edge of the sheet and the lock card
    ("CardRim", 0x4A3E35),  # top edge of a card
    ("Ink2", 0xBFB2A6),  # second-level text, keycap labels
    ("Accent2", 0xC48A4C),  # coat: selected keycaps
    ("OnAccent", 0x1D130B),  # text on caramel
    ("Live", 0x926D45),  # selected card outline
    ("OkBg", 0x272F25),
    ("WarnBg", 0x372F1D),
    ("RecBg", 0x3A2520),  # the status bar's REC capsule
    ("InfoBg", 0x36291C),  # Mando's sign
    ("RecDot", 0xF4675B),  # the recording pages' big dot (to breathe with the animation tick)
]

MAGENTA = 0xFF00FF
# The low bytes of v0.4.0's RGB565 constants: 0x0000 / 0xF800 -> 0x00, 0x8410 / 0x0010 -> 0x10,
# 0x07E0 / 0xFFE0 -> 0xE0, 0xFFFF / 0x07FF -> 0xFF. A stray constant draws magenta.
SENTINELS = (0x00, 0x10, 0xE0, 0xFF)
REC_DOT_EDGE = 0x1A
MANDO_BASE = 0x20  # Mando's 15 colours: m1..m15 at 0x20..0x2E
MANDO_DIM_BASE = 0x30
TOKEN_DIM_BASE = 0x40
AA_FIRST, AA_LAST = 0x60, 0x9F
DIM_T = 0.62  # a dimmed entry is the colour mixed 62 % towards Bg (the Tab sheet's page)
AA_T = 0.5  # one baked edge step: half paint, half what is under it

# The big digits (docs/19 §2.3): 6 levels, 0 clear and 5 the ink; levels 1-4 of Fg are baked
# on the two grounds the digits sit on, four consecutive entries each (k/5 of the way).
DIGIT_LEVELS = 6
DIGIT_RAMPS: list[tuple[str, str, int]] = [("Fg", "Bg", 0xA0), ("Fg", "Sheet", 0xA4)]

# The lock screen's still corner glow (docs/19 §2.3, mockup 06): Bg -> GLOW in 6 steps with a
# 4 x 4 ordered dither, brightest at GLOW_CENTRE, gone at GLOW_RADIUS (y stretched 1.25 x).
GLOW_RGB = 0x6E4521
GLOW_BASE = 0xA8
GLOW_STEPS = 6
GLOW_CENTRE = (-6, -10)
GLOW_RADIUS = 170
GLOW_FALLOFF = 1.6  # step = (1 - d / radius) ** falloff * steps

# Pairs (paint, under) that get an exact 50 % edge entry: every pair the theme draws a rounded
# shape with (lib/theme). Both orders share the entry. Other pairs use the nearest entry.
AA_PAIRS: list[tuple[str, str]] = [
    ("Card", "Bg"),
    ("Sheet", "Bg"),
    ("Select", "Bg"),
    ("Live", "Bg"),
    ("Select", "Live"),
    ("Cap", "Bg"),
    ("Cap", "Card"),
    ("Cap", "Sheet"),
    ("Accent", "Bg"),
    ("Accent", "Card"),
    ("Accent", "Select"),
    ("Accent2", "Accent"),
    ("Raised", "Bg"),
    ("Raised", "Card"),
    ("Raised", "Select"),
    ("Raised", "Sheet"),
    ("OkBg", "Bg"),
    ("OkBg", "Card"),
    ("OkBg", "Select"),
    ("WarnBg", "Bg"),
    ("WarnBg", "Card"),
    ("WarnBg", "Select"),
    ("WarnBg", "Sheet"),
    ("RecBg", "Bg"),
    ("InfoBg", "Bg"),
    ("Warn", "Card"),
    ("Warn", "Select"),  # the inbox's "needs you" stripe on the selected row
    ("Line", "Card"),
    ("Dim", "Line"),
    ("Warn", "Bg"),  # Mando's "!" bubble
    ("Ink2", "Bg"),  # Mando's "?" bubble
    ("Accent2", "Bg"),  # the memo waveform's bar ends (S3)
    ("Error", "Bg"),  # the meeting timeline's Rec dot (S3)
    ("OkBg", "Sheet"),  # docs/20 B1: the lock card's "√ synced" chip and "Saved" sign
    ("Ink2", "Sheet"),  # ... and Mando's "?" bubble on it ("No rec", "Huh?")
    ("Select", "Card"),  # docs/20 B6: a selected Tab-menu row that only explains a key
]

# The big REC dot breathing (docs/19 §2.3): 4 steps of 200 ms, step 0 full Rec. The darkest is
# 4.66:1 on Bg (the judges asked for no less than 4.5:1). The device swaps the RecDot and
# RecDotEdge entries to a step's colours; each step's colours also have entries of their own
# from BREATH_BASE (the device never draws them; the simulator's panel shows what was pushed).
BREATH = [0xF4675B, 0xE05F54, 0xCB574C, 0xE05F54]
BREATH_STEPS = len(BREATH)
BREATH_BASE = 0xAE


def token_index(t: int) -> int:
    """Palette entry of token t: 0x01-0x0F, then 0x11-0x19 (0x00 and 0x10 are sentinels)."""
    return 0x01 + t if t < 15 else 0x11 + (t - 15)


def token_names() -> list[str]:
    return [name for name, _ in TOKENS]


def token_rgb(name: str) -> int:
    return dict(TOKENS)[name]
