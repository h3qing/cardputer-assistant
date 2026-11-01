#!/usr/bin/env python3
"""Firmware budget gate (05 §5.13, docs/09-m2-design.md §5).

  static RAM (.dram0.data + .dram0.bss + .noinit, as PlatformIO counts it) <= 96 KB
  application image (firmware.bin)                                        <= 2.5 MB
  application image over 1.75 MiB (1,835,008 B): WARN only, exit code 0 -- the room
  left to install it next to Bruce 1.16.1 under the Launcher

Usage: python3 scripts/check_budget.py .pio/build/cardputer-adv   (exit 1 when over)
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RAM_MAX = 96 * 1024
APP_MAX = 2560 * 1024
APP_WARN = 1_835_008  # 1.75 MiB
SECTIONS = (".dram0.data", ".dram0.bss", ".noinit")

# An output section at column 0, its address and size on the same or the next line.
_SECTION_RE = re.compile(
    r"^(\.dram0\.data|\.dram0\.bss|\.noinit)[ \t]*\n?[ \t]+0x[0-9a-fA-F]+[ \t]+0x([0-9a-fA-F]+)",
    re.MULTILINE,
)


def static_ram(map_text: str) -> dict[str, int]:
    """Size of each static RAM output section (the first definition in the map)."""
    sizes: dict[str, int] = {}
    for m in _SECTION_RE.finditer(map_text):
        sizes.setdefault(m.group(1), int(m.group(2), 16))
    missing = [s for s in SECTIONS if s not in sizes]
    if missing:
        raise ValueError(f"map file has no {', '.join(missing)}")
    return sizes


def main(argv: list[str]) -> int:
    build = Path(argv[0]) if argv else Path(".pio/build/cardputer-adv")
    map_file, bin_file = build / "firmware.map", build / "firmware.bin"
    if not map_file.is_file() or not bin_file.is_file():
        print(f"budget: FAIL no firmware.map / firmware.bin in {build}")
        return 1
    ram = sum(static_ram(map_file.read_text(errors="replace")).values())
    app = bin_file.stat().st_size
    ok = True
    for name, used, limit in (
        ("static RAM", ram, RAM_MAX),
        ("app image", app, APP_MAX),
    ):
        verdict = "ok" if used <= limit else "FAIL"
        ok = ok and used <= limit
        print(
            f"budget: {verdict} {name} {used:,} / {limit:,} B ({100 * used / limit:.1f}%)"
        )
    if APP_WARN < app <= APP_MAX:
        print(
            f"budget: WARN app image {app:,} B > {APP_WARN:,} B: tight next to Bruce under the Launcher"
        )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
