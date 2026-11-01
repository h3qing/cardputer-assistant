# tools/theme: the visual theme's generator

The firmware draws on a 240 x 135 M5GFX `palette_8bit` canvas whose 256 colours come
from here (docs/19-visual-theme.md §2, §5). The outputs are committed; the firmware build
never runs this and never needs WhisperWoof.

| File | What it is |
|---|---|
| `tokens.py` | The one token table: `cpa::Color` order and colours, the palette layout, the four magenta sentinels, the pairs that get an exact rounded-edge colour (`AA_PAIRS`), the dimming factor, the big digits' ramps, the lock glow, the big ●'s four breath steps (`BREATH`, the darkest 4.66:1 on Bg) |
| `gen_palette.py` | The 256 entries and the tables `kTokenIndex`, `kAa`, `kAaExact`, `kDim`, the digit ramps, the glow's `kGlowQ` and the breath steps' entries `kBreathDot` / `kBreathEdge` (from 0xAE; only the big ● and its rim use RecDot and RecDotEdge, so only it breathes) -> `firmware/app/lib/theme/gen/palette_gen.h` |
| `gen_icons.py` | The status bar's 11 x 9 Mando head as palette entries -> `gen/icons_gen.h` |
| `gen_mando.py` | Mando from WhisperWoof's sprite loops (read only): his 15 colours -> `mando_colours.json` (with the sources' sha256 and the Pillow version); his key poses at 46 and 64 px, 4 bits a pixel -> `gen/mando46_gen.h`, `gen/mando64_gen.h`; their index -> `gen/art_ids.h` |
| `gen_digits.py` | Nunito ExtraBold 0-9 and `:` at 30 and 34 px from WhisperWoof's font file (read only), tabular cells of 6 levels, 4 bits a pixel -> `gen/digits_gen.h` |
| `gen_all.py` | Runs them; `--check` compares instead of writing |
| `tests/` | pytest: layout, sentinels, edges, dimming, ramps, the glow against the mockup's formula, the icon, 4-bit packing, the dozing lids, the key poses, digit levels, and that the committed headers are current (the WhisperWoof ones only with a checkout named by `CPA_WHISPERWOOF` and the Pillow in `mando_colours.json`: `uv run --with pytest --with pillow==13.0.0 pytest tools/theme`) |

## Regenerate

After changing `tokens.py` or `gen_icons.py` (no WhisperWoof needed):

```sh
python3 tools/theme/gen_all.py --palette-only
(cd firmware/app && pio test -e native -f 'core/test_theme_*')
```

Mando's colours and sprites and the digits, from a WhisperWoof checkout (Pillow 13.0.0
reproduces the committed files byte for byte, and the companion mockups' sprites pixel for
pixel; other versions cut them slightly differently):

```sh
uv run --no-project --with pillow==13.0.0 python tools/theme/gen_all.py \
    --whisperwoof /path/to/WhisperWoof
git diff --stat tools/theme/mando_colours.json firmware/app/lib/theme/gen
uv run --no-project --with pytest --with pillow==13.0.0 pytest tools/theme -q
```

CI has no WhisperWoof: it runs `gen_all.py --check --palette-only`, `pytest tools/theme`
(the WhisperWoof tests skip), ruff (`ruff.toml`) and lizard. The sprites and digits are
whatever the committed headers hold.

## Flash

All of it is `const` and stays in flash: Mando 46 px 17 x 598 B and 64 px 12 x 1,152 B,
the digits 4,472 B, the palette and its tables about 2.4 KB; about 31 KB in all
(docs/19 §10: 60 KB at most).

## Adding a colour

Add the token to `tokens.py` and to `cpa::Color` (`firmware/app/lib/module_api/canvas.h`)
in the same order, regenerate, and add the text pairs it is drawn in to
`test/core/test_theme_palette`. A new rounded shape on a new pair of colours goes into
`AA_PAIRS`; without it the edge uses the nearest colour and the simulator's
`test_theme_sim` (no edge misses on the tour) fails.

## Licences

WhisperWoof is MIT, Copyright (c) 2026 Heqing Huang (the same author); the tokens, Mando's
colours and his sprites are recorded in the repository's NOTICE, section 4. Nunito is SIL
OFL 1.1 (Copyright 2014 The Nunito Project Authors): NOTICE section 4 and
`LICENSES/OFL-1.1.txt`.
