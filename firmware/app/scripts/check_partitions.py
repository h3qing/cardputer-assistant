#!/usr/bin/env python3
"""Partition table gate (docs/09-m2-design.md §4.2).

The Launcher reads the table inside our merged image and creates data partitions by
label, so the built partitions.bin must keep three promises:
  - "cpafs" (data / spiffs) at 0x670000, 0x180000 bytes: v0.1's spiffs region, so a
    full-flash upgrade mounts v0.1's data;
  - no partition labelled "spiffs": under the Launcher that one belongs to other firmware;
  - no app partition of subtype test (0x20): it would fool our Launcher detection.

Usage: python3 scripts/check_partitions.py .pio/build/cardputer-adv/partitions.bin
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path
from typing import NamedTuple

ENTRY = struct.Struct("<2sBBII16sI")  # magic, type, subtype, offset, size, label, flags
MAGIC = b"\xaa\x50"
MD5_MAGIC = b"\xeb\xeb"
TYPE_APP, TYPE_DATA = 0x00, 0x01
SUBTYPE_TEST, SUBTYPE_SPIFFS = 0x20, 0x82
CPAFS = ("cpafs", 0x670000, 0x180000)


class Partition(NamedTuple):
    label: str
    type: int
    subtype: int
    offset: int
    size: int


def encode_entry(label: str, ptype: int, subtype: int, offset: int, size: int) -> bytes:
    raw = label.encode()
    if len(raw) > 16:
        raise ValueError(f"label longer than 16 bytes: {label!r}")
    return ENTRY.pack(MAGIC, ptype, subtype, offset, size, raw.ljust(16, b"\0"), 0)


def decode(data: bytes) -> list[Partition]:
    """Entries up to the end marker; the MD5 entry is skipped. ValueError on garbage."""
    rows = []
    for at in range(0, len(data) - ENTRY.size + 1, ENTRY.size):
        magic, ptype, subtype, offset, size, label, _ = ENTRY.unpack_from(data, at)
        if magic == MD5_MAGIC:
            continue
        if magic != MAGIC:
            break
        rows.append(Partition(label.rstrip(b"\0").decode(errors="replace"), ptype, subtype, offset, size))
    if not rows:
        raise ValueError("no partition entries")
    return rows


def problems(rows: list[Partition]) -> list[str]:
    found = []
    label, offset, size = CPAFS
    cpafs = [r for r in rows if r.label == label]
    if not cpafs:
        found.append("no cpafs partition")
    for r in cpafs:
        if (r.type, r.subtype, r.offset, r.size) != (TYPE_DATA, SUBTYPE_SPIFFS, offset, size):
            found.append(f"cpafs must be data/spiffs at {offset:#x}, {size:#x} B (is {r.offset:#x}, {r.size:#x} B)")
    found += [f"partition labelled spiffs at {r.offset:#x}" for r in rows if r.label == "spiffs"]
    found += [
        f"app partition of subtype test: {r.label}"
        for r in rows
        if r.type == TYPE_APP and r.subtype == SUBTYPE_TEST
    ]
    return found


def main(argv: list[str]) -> int:
    path = Path(argv[0]) if argv else Path(".pio/build/cardputer-adv/partitions.bin")
    try:
        rows = decode(path.read_bytes())
    except (OSError, ValueError) as e:
        print(f"partitions: FAIL cannot read {path}: {e}")
        return 1
    for r in rows:
        print(f"partitions: {r.label:<10} type {r.type:#04x}/{r.subtype:#04x} at {r.offset:#09x} size {r.size:#09x}")
    bad = problems(rows)
    for p in bad:
        print(f"partitions: FAIL {p}")
    print(f"partitions: {'ok' if not bad else 'FAIL'} {len(rows)} entries")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
