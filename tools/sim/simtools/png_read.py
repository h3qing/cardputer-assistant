"""Reads cpa-sim's screenshots with the standard library: 8-bit indexed PNG, every row with filter 0.

That is all firmware/sim/lib/simpng writes, so anything else is an error rather than a decoder to maintain.
"""

from __future__ import annotations

import struct
import zlib
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(ValueError):
    """Not a PNG cpa-sim writes."""


@dataclass(frozen=True, slots=True)
class Indexed:
    width: int
    height: int
    palette: tuple[tuple[int, int, int], ...]
    pixels: bytes  # width * height palette indexes, row by row

    def index(self, x: int, y: int) -> int:
        return self.pixels[y * self.width + x]

    def rgb(self, x: int, y: int) -> tuple[int, int, int]:
        return self.palette[self.index(x, y)]

    def count(self, rgb: tuple[int, int, int]) -> int:
        """Pixels of this colour."""
        wanted = {i for i, c in enumerate(self.palette) if c == rgb}
        return sum(1 for p in self.pixels if p in wanted)

    def count_index(self, index: int) -> int:
        """Pixels of this palette entry; in cpa-sim's PNGs the index is the sprite's RGB332 byte."""
        return self.pixels.count(index)


def _chunks(data: bytes) -> Iterator[tuple[bytes, bytes]]:
    at = len(SIGNATURE)
    while at + 12 <= len(data):
        (n,) = struct.unpack(">I", data[at : at + 4])
        kind, body = data[at + 4 : at + 8], data[at + 8 : at + 8 + n]
        (crc,) = struct.unpack(">I", data[at + 8 + n : at + 12 + n])
        if zlib.crc32(kind + body) != crc:
            raise PngError(f"bad CRC in {kind.decode('ascii', 'replace')}")
        yield kind, body
        at += 12 + n


def _rows(raw: bytes, width: int, height: int) -> bytes:
    stride = width + 1
    if len(raw) != stride * height:
        raise PngError("image data has the wrong size")
    if any(raw[y * stride] != 0 for y in range(height)):
        raise PngError("a row uses a filter other than 0")
    return b"".join(raw[y * stride + 1 : (y + 1) * stride] for y in range(height))


def read_png_bytes(data: bytes) -> Indexed:
    if not data.startswith(SIGNATURE):
        raise PngError("no PNG signature")
    chunks = list(_chunks(data))
    head = dict(chunks).get(b"IHDR", b"")
    if len(head) != 13:
        raise PngError("no IHDR")
    width, height, depth, ctype = struct.unpack(">IIBB", head[:10])
    if (depth, ctype) != (8, 3):
        raise PngError("not 8-bit indexed colour")
    plte = dict(chunks).get(b"PLTE", b"")
    palette = tuple(tuple(plte[i : i + 3]) for i in range(0, len(plte) - 2, 3))
    raw = zlib.decompress(b"".join(body for kind, body in chunks if kind == b"IDAT"))
    return Indexed(width, height, palette, _rows(raw, width, height))  # type: ignore[arg-type]


def read_png(path: Path) -> Indexed:
    return read_png_bytes(Path(path).read_bytes())
