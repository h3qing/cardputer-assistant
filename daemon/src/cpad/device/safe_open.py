"""SafeOpen (03 §2 pit 3, 05 §7.2): open the USB-Serial/JTAG port without resetting the chip.

ESP32-S3 USB-Serial/JTAG truth table for (RTS, DTR): (1, 0) resets the chip. macOS raises both
lines when the port opens, so the only safe order is the one esp-idf-monitor uses:
dsrdtr=True (pyserial leaves DTR alone on open) -> open -> rts=False (0, 1) -> dtr=False (0, 0).
Closing afterwards cannot change anything because both lines are already low.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

import serial

BAUDRATE = 115200


def safe_open(
    path: str, factory: Callable[[], Any] = serial.Serial, *, timeout: float = 0.1, write_timeout: float = 1.0
) -> Any:
    port = factory()
    port.port = path
    port.baudrate = BAUDRATE
    port.timeout = timeout
    port.write_timeout = write_timeout
    port.exclusive = True
    port.dsrdtr = True
    port.open()
    try:
        port.rts = False
        port.dtr = False
    except Exception:
        port.close()
        raise
    return port
