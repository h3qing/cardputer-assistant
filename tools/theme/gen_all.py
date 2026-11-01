#!/usr/bin/env python3
"""Regenerates the theme's committed outputs (docs/19-visual-theme.md §5).

  gen_all.py --whisperwoof PATH   everything, Mando and the digits from WhisperWoof (read only)
  gen_all.py --palette-only       the headers that need no WhisperWoof, from mando_colours.json
  --check                         compare instead of writing; exit 1 when anything differs

Outputs: tools/theme/mando_colours.json and firmware/app/lib/theme/gen/: palette_gen.h,
icons_gen.h and art_ids.h always; mando46_gen.h, mando64_gen.h and digits_gen.h only with
--whisperwoof. The firmware build never runs this; CI runs --check --palette-only.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_digits  # noqa: E402
import gen_icons  # noqa: E402
import gen_mando  # noqa: E402
import gen_palette  # noqa: E402

REPO = HERE.parent.parent
GEN = REPO / "firmware" / "app" / "lib" / "theme" / "gen"
COLOURS = HERE / "mando_colours.json"


def outputs(whisperwoof: Path | None) -> dict[Path, str]:
    """Every output file and the text it should hold."""
    out: dict[Path, str] = {}
    if whisperwoof is not None:
        data = gen_mando.build(whisperwoof)
        out[COLOURS] = gen_mando.render(data)
        for h in gen_mando.SIZES:
            out[GEN / f"mando{h}_gen.h"] = gen_mando.sprite_header(h, data)
        font = gen_digits.font_path(whisperwoof)
        out[GEN / "digits_gen.h"] = gen_digits.header(
            gen_digits.build(whisperwoof), gen_mando.sha256(font), data["pillow"]
        )
    else:
        data = json.loads(COLOURS.read_text())
    mando = gen_mando.parse_colours(data)
    out[GEN / "palette_gen.h"] = gen_palette.header(gen_palette.Palette(mando))
    out[GEN / "icons_gen.h"] = gen_icons.header()
    out[GEN / "art_ids.h"] = gen_mando.ids_header(data["box"])
    return out


def run(whisperwoof: Path | None, check: bool) -> int:
    stale = []
    for path, text in outputs(whisperwoof).items():
        old = path.read_text() if path.exists() else None
        if old == text:
            continue
        stale.append(path)
        if not check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
    for path in stale:
        print(f"{'differs' if check else 'wrote'}: {path.relative_to(REPO)}")
    return 1 if check and stale else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--whisperwoof", type=Path, help="a WhisperWoof checkout (read only)")
    src.add_argument("--palette-only", action="store_true", help="use the committed mando_colours.json")
    ap.add_argument("--check", action="store_true", help="compare only; exit 1 on any difference")
    args = ap.parse_args(argv)
    return run(None if args.palette_only else args.whisperwoof, args.check)


if __name__ == "__main__":
    sys.exit(main())
