"""Flash the Launcher's merged image at 0x0 with PlatformIO's esptool (docs/gates/launcher.md §4).

No G0: on the USB-Serial/JTAG esptool enters download mode by itself and hard-resets afterwards
(docs/03 §2 pit 2). That reset is a USB reset, and with no OTA app in the new table the bootloader
falls back to the Launcher's test partition, so the Launcher comes up by itself. The command is the one
used on 2026-10-20 (tool-esptoolpy 5.0.2 still accepts esptool.py / write_flash, with a warning).
"""

from __future__ import annotations

import subprocess
from collections.abc import Callable
from pathlib import Path
from typing import Any

BAUD = 460800


def find_pio(which: Callable[[str], str | None], home: Path) -> str | None:
    found = which("pio")
    if found:
        return found
    local = home / ".local" / "bin" / "pio"  # `uv tool install platformio` (docs/03 §4)
    return str(local) if local.is_file() else None


def flash_command(pio: str, port: str, image: Path, baud: int = BAUD) -> list[str]:
    return [
        pio, "pkg", "exec", "--package", "tool-esptoolpy", "--",
        "esptool.py", "--chip", "esp32s3", "--port", port, "--baud", str(baud),
        "write_flash", "0x0", str(image),
    ]  # fmt: skip


def write_flash(cmd: list[str], run: Callable[..., Any] = subprocess.run) -> int:
    """Run esptool with its progress on the terminal; the exit code (127 when it cannot start)."""
    try:
        return run(cmd, check=False).returncode
    except OSError:
        return 127
