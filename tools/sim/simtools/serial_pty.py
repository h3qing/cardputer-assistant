"""pyserial on the simulator's pty, and a port lister that shows that pty as the Cardputer.

A pty has no modem lines: setting RTS / DTR fails with ENOTTY (errno 25), so cpad's SafeOpen would give
up. PtySerial makes those two updates no-ops; the rest is pyserial's own POSIX port, so SafeOpen,
the exclusive lock, timeouts and cancel_read() run unchanged. sim_lister() reports only the pty, with
the Cardputer's USB ids (PortFinder takes nothing else) and a serial number no real board has, so a
cpad given it can never pick a real device.
"""

from __future__ import annotations

from collections.abc import Callable

import serial
from cpad.device.port_finder import ESPRESSIF_VID, USB_JTAG_PID, PortInfo

SIM_SERIAL = "SIM-100000000051"


class PtySerial(serial.Serial):
    def _update_rts_state(self) -> None:
        """A pty has no RTS line."""

    def _update_dtr_state(self) -> None:
        """A pty has no DTR line."""


def sim_port(path: str) -> PortInfo:
    return PortInfo(path, SIM_SERIAL, ESPRESSIF_VID, USB_JTAG_PID)


def sim_lister(path: str) -> Callable[[], tuple[PortInfo, ...]]:
    port = sim_port(path)
    return lambda: (port,)
