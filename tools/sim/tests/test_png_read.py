"""The stdlib PNG reader the tests use on cpa-sim's screenshots (8-bit indexed, filter 0)."""

import struct
import zlib

import pytest

from simtools.png_read import PngError, read_png_bytes


def chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def png(width: int, height: int, rows: list[bytes], palette: list[tuple[int, int, int]], *, depth: int = 8,
        ctype: int = 3, filt: int = 0) -> bytes:  # fmt: skip
    raw = b"".join(bytes([filt]) + r for r in rows)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, depth, ctype, 0, 0, 0))
            + chunk(b"PLTE", b"".join(bytes(c) for c in palette))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))  # fmt: skip


PAL = [(0, 0, 0), (255, 0, 0), (0, 255, 0)]


def test_reads_size_palette_and_pixels():
    img = read_png_bytes(png(3, 2, [b"\x00\x01\x02", b"\x02\x02\x00"], PAL))
    assert (img.width, img.height) == (3, 2)
    assert img.index(1, 0) == 1 and img.rgb(1, 0) == (255, 0, 0)
    assert img.rgb(0, 1) == (0, 255, 0)
    assert img.count((0, 255, 0)) == 3
    assert img.count_index(2) == 3 and img.count_index(1) == 1


def test_rejects_what_cpa_sim_never_writes():
    with pytest.raises(PngError, match="signature"):
        read_png_bytes(b"GIF89a")
    with pytest.raises(PngError, match="indexed"):
        read_png_bytes(png(1, 1, [b"\x00\x00\x00"], PAL, ctype=2))
    with pytest.raises(PngError, match="filter"):
        read_png_bytes(png(1, 1, [b"\x00"], PAL, filt=1))


def test_rejects_a_bad_crc():
    data = bytearray(png(1, 1, [b"\x00"], PAL))
    data[29] ^= 0xFF  # inside IHDR's CRC
    with pytest.raises(PngError, match="CRC"):
        read_png_bytes(bytes(data))
