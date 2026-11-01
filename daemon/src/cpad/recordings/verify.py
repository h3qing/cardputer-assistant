"""Segment verification (docs/11 §6.2.8). Pure: the I/O (reading, hashing) lives in land.py.

A pulled segment is accepted when:
  - its size is the listed size, at least the 44 B header, with an even data length (16-bit samples);
  - its header equals the standard 16 kHz / 16 bit / mono PCM header rebuilt from that length. A
    recovered (rcv) segment may differ: the header is then replaced and the segment marked repaired;
  - SHA-256 of data[44:] equals the device's value (skipped for rcv segments, which have none).
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from cpad.protocol.file_messages import FileItem

HEADER = 44
RATE = 16000
BITS = 16
CHANNELS = 1


@dataclass(frozen=True, slots=True)
class Verdict:
    ok: bool
    repaired: bool = False
    reason: str | None = None


def wav_header(data_len: int) -> bytes:
    block = CHANNELS * BITS // 8
    fmt = struct.pack("<IHHIIHH", 16, 1, CHANNELS, RATE, RATE * block, block, BITS)
    return b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVEfmt " + fmt + b"data" + struct.pack("<I", data_len)


def check(item: FileItem, size: int, header: bytes, data_sha256: str) -> Verdict:
    data_len = size - HEADER
    if size != item.size or data_len < 0 or data_len % 2:
        return Verdict(False, reason="size")
    repaired = header != wav_header(data_len)
    if repaired and not item.rcv:
        return Verdict(False, reason="header")
    if not item.rcv and item.sha256 is not None and item.sha256 != data_sha256:
        return Verdict(False, reason="sha256")
    return Verdict(True, repaired=repaired)
