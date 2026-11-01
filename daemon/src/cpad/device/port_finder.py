"""PortFinder (05 §7.2, US-04): pick the Cardputer's USB-Serial/JTAG port.

Only 303A:1001 ports count. Once a serial number is bound, every other board is ignored.
A port whose handshake failed is left alone for 30 s (keyed by serial number, else by path).
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass, field
from types import MappingProxyType

from serial.tools.list_ports import comports

ESPRESSIF_VID = 0x303A
USB_JTAG_PID = 0x1001


@dataclass(frozen=True, slots=True)
class PortInfo:
    device: str
    serial: str | None
    vid: int | None
    pid: int | None

    @property
    def key(self) -> str:
        return self.serial or self.device


@dataclass(frozen=True, slots=True)
class Backoff:
    until: MappingProxyType = field(default_factory=lambda: MappingProxyType({}))

    def hold(self, port: PortInfo, now: float, seconds: float) -> Backoff:
        return Backoff(MappingProxyType({**self.until, port.key: now + seconds}))

    def blocked(self, port: PortInfo, now: float) -> bool:
        return now < self.until.get(port.key, float("-inf"))


def list_ports() -> tuple[PortInfo, ...]:
    return tuple(PortInfo(p.device, p.serial_number, p.vid, p.pid) for p in comports())


def _is_cardputer(port: PortInfo) -> bool:
    return (port.vid, port.pid) == (ESPRESSIF_VID, USB_JTAG_PID)


def choose(ports: Iterable[PortInfo], bound_serial: str | None, backoff: Backoff, now: float) -> PortInfo | None:
    candidates = sorted((p for p in ports if _is_cardputer(p)), key=lambda p: p.device)
    if bound_serial:
        candidates = [p for p in candidates if p.serial == bound_serial]
    return next((p for p in candidates if not backoff.blocked(p, now)), None)
