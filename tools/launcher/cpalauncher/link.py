"""Serial I/O shell for the Launcher console: safe-open, port discovery, lsof, and a line reader.

SafeOpen (docs/03 §2 pit 3; the daemon's cpad/device/safe_open.py does the same): on the ESP32-S3
USB-Serial/JTAG, (RTS, DTR) = (1, 0) resets the chip and macOS raises both lines on open, so:
dsrdtr=True (pyserial leaves DTR alone on open) -> open -> rts=False (0, 1) -> dtr=False (0, 0).
Every I/O error (unplug, reboot re-enumeration) surfaces as PortLost.
"""

from __future__ import annotations

import os
import subprocess
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from typing import Any

import serial

ESPRESSIF_VID = 0x303A
USB_JTAG_PID = 0x1001
BAUDRATE = 115200
LINE_CAP = 8192  # the Launcher drops a line longer than this too


class PortLost(Exception):
    """The port went away or failed: device unplugged, rebooting, or held by someone else."""


@dataclass(frozen=True, slots=True)
class PortInfo:
    device: str
    vid: int | None
    pid: int | None


def list_ports() -> tuple[PortInfo, ...]:
    from serial.tools.list_ports import comports

    return tuple(PortInfo(p.device, p.vid, p.pid) for p in comports())


def find_port(ports: Iterable[PortInfo]) -> str | None:
    """The Cardputer's USB-Serial/JTAG port (303A:1001); the first by name when several are plugged in."""
    found = sorted(p.device for p in ports if (p.vid, p.pid) == (ESPRESSIF_VID, USB_JTAG_PID))
    return found[0] if found else None


def parse_lsof(out: str, own_pid: int) -> tuple[int, ...]:
    pids = (int(tok) for tok in out.split() if tok.isdigit())
    return tuple(pid for pid in pids if pid != own_pid)


def busy_pids(device: str, run: Callable[..., Any] = subprocess.run, own_pid: int | None = None) -> tuple[int, ...]:
    """Other processes holding `device` open (lsof -t); empty when none or when lsof is unavailable."""
    try:
        result = run(["lsof", "-t", device], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return ()
    return parse_lsof(result.stdout or "", os.getpid() if own_pid is None else own_pid)


def safe_open(path: str, factory: Callable[[], Any] = serial.Serial, timeout: float = 0.2,
              write_timeout: float = 5.0) -> Any:  # fmt: skip
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


def split_lines(buf: bytes, cap: int = LINE_CAP) -> tuple[tuple[str, ...], bytes]:
    """Complete, non-empty lines (CR stripped, bad UTF-8 replaced) and the unfinished tail."""
    *done, tail = buf.split(b"\n")
    lines = tuple(t for t in (raw.decode("utf-8", "replace").strip() for raw in done) if t)
    return lines, (tail if len(tail) <= cap else b"")


_IO_ERRORS = (serial.SerialException, OSError)


class Console:
    """An open Launcher console port. Owns the receive buffer; everything else is the port's."""

    def __init__(self, port: Any, clock: Callable[[], float]) -> None:
        self._port, self._clock, self._buf = port, clock, b""

    def send(self, line: str) -> None:
        if "\n" in line or "\r" in line:
            raise ValueError("one command per line")
        self.write((line + "\n").encode())

    def write(self, data: bytes) -> None:
        try:
            self._port.write(data)
        except _IO_ERRORS as exc:
            raise PortLost(str(exc)) from exc

    def drain(self) -> None:
        """Forget anything already received (stale banners, an old reply)."""
        self._buf = b""
        try:
            self._port.reset_input_buffer()
        except _IO_ERRORS as exc:
            raise PortLost(str(exc)) from exc

    def poll(self, wait_s: float) -> tuple[str, ...]:
        """Lines that complete within `wait_s`; returns as soon as there is at least one."""
        deadline = self._clock() + wait_s
        while True:
            lines, self._buf = split_lines(self._buf)
            if lines or self._clock() >= deadline:
                return lines
            self._buf += self._read()

    def collect(self, wait_s: float, stop: Callable[[str], bool] | None = None) -> tuple[str, ...]:
        """Every line for `wait_s`, or up to and including the first line `stop` accepts."""
        deadline, got = self._clock() + wait_s, ()
        while self._clock() < deadline:
            for line in self.poll(deadline - self._clock()):
                got = (*got, line)
                if stop is not None and stop(line):
                    return got
        return got

    def _read(self) -> bytes:
        try:
            return self._port.read(self._port.in_waiting or 1)
        except _IO_ERRORS as exc:
            raise PortLost(str(exc)) from exc

    def close(self) -> None:
        try:
            self._port.close()
        except _IO_ERRORS:
            pass


class Opener:
    """open_console() for the wait loop: find the port, refuse while another process holds it, safe-open."""

    def __init__(self, ports: Callable[[], Iterable[PortInfo]], run: Callable[..., Any],
                 factory: Callable[[], Any], clock: Callable[[], float], say: Callable[[str], None],
                 device: str | None = None) -> None:  # fmt: skip
        self._ports, self._run, self._factory, self._clock, self._say = ports, run, factory, clock, say
        self._device = device
        self._told: frozenset[int] = frozenset()  # pids already reported as holding the port

    def __call__(self) -> Console | None:
        device = self._device or find_port(self._ports())
        if device is None:
            return None
        busy = busy_pids(device, self._run)
        if busy:
            if not set(busy) <= self._told:
                self._say(f"{device} is held by pid {', '.join(map(str, busy))}; waiting for it to let go")
                self._told = self._told | frozenset(busy)
            return None
        try:
            return Console(safe_open(device, self._factory), self._clock)
        except _IO_ERRORS:
            return None
