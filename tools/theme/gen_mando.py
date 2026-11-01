"""Mando from WhisperWoof's HD sprite loops (read only), as the companion mockups built him
(docs/19 §5.1, §7.1): every frame of wait / think / review / hop cropped to their union box,
scaled to 46 and 64 px, composited on the Sheet colour (the device has no alpha; under 30 %
cover is clear), and cut to one set of 15 colours (median cut of the 64 px frames).

Writes tools/theme/mando_colours.json (the colours; gen_palette.py reads it, so the palette is
rebuilt without WhisperWoof), the key poses packed 4 bits a pixel into
lib/theme/gen/mando46_gen.h and mando64_gen.h, and their index lib/theme/gen/art_ids.h.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ACTIONS = ("wait", "think", "review", "hop")
HEIGHT = 64  # the colours are cut from this size
SIZES = (46, 64)
# The poses the device keeps (docs/19 §7.1), in header order; 46 px also has "doze", wait 0
# with the eyes closed. 4 poses of 200 ms each; hop 6 and review 6 are mockup 07's.
KEYS: dict[int, list[tuple[str, tuple[int, ...]]]] = {
    46: [("wait", (0, 6, 12, 18)), ("review", (0, 6, 12, 18)), ("think", (0, 6, 12, 18)), ("hop", (2, 6, 10, 14))],
    64: [("review", (0, 6, 12, 18)), ("wait", (0, 6, 12, 18)), ("think", (0, 6, 12, 18))],
}
POSES = 4
BEHIND = (0x1A, 0x15, 0x12)  # Sheet: Mando always sits on Bg or Sheet
ALPHA_BOX = 40  # a pixel counts for the crop box above this alpha
ALPHA_OPAQUE = 77  # and as opaque (30 %) at or above this one
COLOURS = 15


def sprite_path(whisperwoof: Path, action: str) -> Path:
    return whisperwoof / "website" / "mando" / "hd" / f"{action}.webp"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def frames_of(path: Path) -> list:
    from PIL import Image

    im = Image.open(path)
    out = []
    for i in range(im.n_frames):
        im.seek(i)
        out.append(im.convert("RGBA").copy())
    return out


def union_box(frames: list) -> tuple[int, int, int, int]:
    box = None
    for f in frames:
        b = f.getchannel("A").point(lambda a: 255 if a > ALPHA_BOX else 0).getbbox()
        if b is not None:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    if box is None:
        raise ValueError("no opaque pixel in any frame")
    return box


def width_of(box, h: int) -> int:
    return round((box[2] - box[0]) * h / (box[3] - box[1]))


def on_sheet(frame, box, h: int):
    """The frame cropped, scaled to h px and composited on BEHIND (RGB), and its alpha."""
    from PIL import Image

    c = frame.crop(box)
    s = c.resize((width_of(box, h), h), Image.LANCZOS, reducing_gap=3.0)
    bg = Image.new("RGBA", s.size, (*BEHIND, 255))
    bg.alpha_composite(s)
    return bg.convert("RGB"), s.getchannel("A")


def opaque_pixels(frame, box) -> list[tuple[int, int, int]]:
    """The frame at HEIGHT on BEHIND: its pixels at >= 30 % alpha."""
    rgb_img, alpha_img = on_sheet(frame, box, HEIGHT)
    rgb, alpha = rgb_img.load(), alpha_img.load()
    return [rgb[x, y] for y in range(rgb_img.height) for x in range(rgb_img.width) if alpha[x, y] >= ALPHA_OPAQUE]


def indexed(frame, box, h: int, colours: list[tuple[int, int, int]]) -> list[list[int]]:
    """The frame at h px as rows of 0 (clear) or n, Mando colour n (1-15)."""
    from PIL import Image

    rgb, alpha = on_sheet(frame, box, h)
    pal = Image.new("P", (1, 1))
    pal.putpalette([v for c in colours for v in c])
    q = rgb.quantize(colors=COLOURS, palette=pal, dither=Image.Dither.NONE).load()
    a = alpha.load()
    return [[q[x, y] + 1 if a[x, y] >= ALPHA_OPAQUE else 0 for x in range(rgb.width)] for y in range(h)]


def dozing(rows: list[list[int]]) -> list[list[int]]:
    """A copy of 46 px wait 0 with the eyes closed: two 2 px lids, lit coat above them
    (mockup 06, s_lock.py)."""
    out = [list(r) for r in rows]
    for x in (6, 7, 12, 13):
        out[10][x] = 14
        out[9][x] = 5
    out[11][7] = 9
    out[11][12] = 9
    return out


def key_names(h: int) -> list[tuple[str, tuple[str, int]]]:
    """("wait 0", ("wait", 0)) for every pose kept at h px, in header order."""
    out = [(f"{a} {i}", (a, i)) for a, poses in KEYS[h] for i in poses]
    return out + [("doze", ("wait", 0))] if h == 46 else out


def key_frames(frames: dict, box, h: int, colours) -> list[tuple[str, list[list[int]]]]:
    out = []
    for name, (action, i) in key_names(h):
        rows = indexed(frames[action][i], box, h, colours)
        out.append((name, dozing(rows) if name == "doze" else rows))
    return out


def pack4(rows: list[list[int]]) -> bytes:
    """Row-major, two pixels a byte, the first in the high nibble; an odd last pixel pads."""
    flat = [v for r in rows for v in r]
    if any(not 0 <= v <= 15 for v in flat):
        raise ValueError("a 4-bit pixel is 0-15")
    flat += [0] * (len(flat) % 2)
    return bytes((flat[i] << 4) | flat[i + 1] for i in range(0, len(flat), 2))


def unpack4(data: bytes, w: int, h: int) -> list[list[int]]:
    flat = [v for b in data for v in (b >> 4, b & 15)]
    return [flat[y * w : (y + 1) * w] for y in range(h)]


def quantise(pixels: list[tuple[int, int, int]]) -> list[str]:
    from PIL import Image

    strip = Image.new("RGB", (len(pixels), 1))
    strip.putdata(pixels)
    q = strip.quantize(colors=COLOURS, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[: COLOURS * 3]
    return ["#" + "".join(f"{v:02x}" for v in pal[i : i + 3]) for i in range(0, len(pal), 3)]


def build(whisperwoof: Path) -> dict:
    """The colours (mando_colours.json) and the key frames at both sizes."""
    import PIL

    paths = {a: sprite_path(whisperwoof, a) for a in ACTIONS}
    frames = {a: frames_of(p) for a, p in paths.items()}
    box = union_box([f for fs in frames.values() for f in fs])
    pixels = [px for a in ACTIONS for f in frames[a] for px in opaque_pixels(f, box)]
    colours = quantise(pixels)
    rgb = parse_colours({"colours": colours})
    return {
        "colours": colours,
        "box": list(box),
        "height": HEIGHT,
        "sources": {f"website/mando/hd/{a}.webp": sha256(p) for a, p in paths.items()},
        "pillow": PIL.__version__,
        "frames": {h: key_frames(frames, box, h, rgb) for h in SIZES},
    }


def render(data: dict) -> str:
    """mando_colours.json: everything but the frames."""
    return json.dumps({k: v for k, v in data.items() if k != "frames"}, indent=2, sort_keys=True) + "\n"


def parse_colours(data: dict) -> list[tuple[int, int, int]]:
    return [tuple(int(c[i : i + 2], 16) for i in (1, 3, 5)) for c in data["colours"]]


def load_colours(path: Path) -> list[tuple[int, int, int]]:
    return parse_colours(json.loads(path.read_text()))


GUARD_OPEN = ["#pragma once", "", "#include <cstdint>", ""]
NAMESPACE_OPEN = ["namespace cpa {", "namespace theme {", "namespace gen {", ""]
NAMESPACE_CLOSE = ["}  // namespace gen", "}  // namespace theme", "}  // namespace cpa", ""]
BYTES_PER_LINE = 48


def frame_bytes(w: int, h: int) -> int:
    return (w * h + 1) // 2


def ids_header(box) -> str:
    """art_ids.h: the frames' sizes and where each act's poses start (no pixels)."""
    lines = [
        "// Generated by tools/theme/gen_all.py (gen_mando.py). Do not edit; regenerate.",
        "// Mando's key poses (docs/19 §7.1): sizes, and where each act's poses start in",
        "// kMando46 / kMando64 (mando46_gen.h, mando64_gen.h).",
        *GUARD_OPEN,
        *NAMESPACE_OPEN,
        f"constexpr uint8_t kMandoPoses = {POSES};",
    ]
    for h in SIZES:
        w = width_of(box, h)
        names = key_names(h)
        lines += [
            "",
            f"constexpr int16_t kMando{h}W = {w};",
            f"constexpr int16_t kMando{h}H = {h};",
            f"constexpr uint16_t kMando{h}Bytes = {frame_bytes(w, h)};",
            f"constexpr uint8_t kMando{h}Frames = {len(names)};",
        ]
        for k, (action, _) in enumerate(KEYS[h]):
            lines.append(f"constexpr uint8_t kM{h}{action.capitalize()} = {k * POSES};")
        if h == 46:
            lines.append(f"constexpr uint8_t kM46Doze = {len(names) - 1};")
    return "\n".join([*lines, "", *NAMESPACE_CLOSE])


def sprite_header(h: int, data: dict) -> str:
    """mando<h>_gen.h: the key poses, 4 bits a pixel, high nibble first."""
    frames = data["frames"][h]
    w = len(frames[0][1][0])
    sources = [f"//   {p} {sha[:16]}" for p, sha in sorted(data["sources"].items())]
    lines = [
        "// Generated by tools/theme/gen_all.py (gen_mando.py). Do not edit; regenerate.",
        f"// Mando, {w} x {h}, from WhisperWoof (MIT, NOTICE) with Pillow {data['pillow']}:",
        *sources,
        "// 4 bits a pixel, row-major, the first pixel in the high nibble; 0 is clear and n is",
        "// Mando colour n (palette entry 0x1F + n). Only lib/theme/theme_art.cpp includes this.",
        *GUARD_OPEN,
        '#include "art_ids.h"',
        "",
        *NAMESPACE_OPEN,
        f"constexpr uint8_t kMando{h}[kMando{h}Frames][kMando{h}Bytes] = {{",
    ]
    for name, rows in frames:
        lines.append(f"    {{  // {name}")
        packed = pack4(rows)
        lines += [
            "        " + ",".join(f"0x{v:02X}" for v in packed[i : i + BYTES_PER_LINE]) + ","
            for i in range(0, len(packed), BYTES_PER_LINE)
        ]
        lines.append("    },")
    return "\n".join([*lines, "};", "", *NAMESPACE_CLOSE])
