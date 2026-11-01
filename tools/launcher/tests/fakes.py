"""Test doubles: a virtual clock, a serial port that speaks the Launcher 2.9.1 console, and `cpa` / `lsof`.

FakeLauncher mirrors bmorcelli/Launcher src/serial_console.cpp (2.9.1 has the same strings): one command
per line, "READY <size>", one "ACK <written>/<size>" per chunk read, then "OK flashed, rebooting" and a
reboot. Reads block for `timeout` seconds of *virtual* time when nothing is pending.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass, replace

import serial

BANNER = "Press the button to enter the Launcher!"
CHUNK = 2048


class FakeClock:
    def __init__(self, t: float = 1000.0) -> None:
        self.t = t

    def __call__(self) -> float:
        return self.t

    def sleep(self, seconds: float) -> None:
        self.t += max(0.0, seconds)


@dataclass(frozen=True)
class Part:
    label: str
    type: str
    subtype: str
    offset: int
    size: int
    boot: bool = False


def standard_table(boot: str | None = None, extra: tuple[Part, ...] = ()) -> tuple[Part, ...]:
    """Launcher 2.9.1 on an 8 MB ADV with the factory app and cpa-v0.2.0 installed (G-L1, 2026-10-20)."""
    parts = (
        Part("nvs", "data", "0x02", 0x9000, 0x5000),
        Part("otadata", "data", "0x00", 0xE000, 0x2000),
        Part("app0", "app", "0x20", 0x10000, 0x150000),
        Part("coredump", "data", "0x03", 0x160000, 0x10000),
        Part("factor", "app", "ota_0", 0x170000, 0x280000),
        Part("cpav02", "app", "ota_1", 0x3F0000, 0xE0000),
        *extra,
    )
    return tuple(replace(p, boot=p.label == boot) for p in parts)


def _human(size: int) -> str:
    return f"{size / 1024 / 1024:.1f} MB" if size >= 1024 * 1024 else f"{size // 1024} KB"


def free_ranges(parts: tuple[Part, ...], flash: int = 0x800000) -> tuple[tuple[int, int], ...]:
    end = max(p.offset + p.size for p in parts)
    return ((end, flash - end),) if end < flash else ()


def render_table(parts: tuple[Part, ...]) -> tuple[str, ...]:
    """Exactly the printf formats of printPartitionTable()."""
    rows = [
        f"{p.label:<16} type={p.type:<4} subtype={p.subtype:<9} offset=0x{p.offset:06X} size=0x{p.size:06X} "
        f"({_human(p.size)}){'  <-- BOOT' if p.boot else ''}"
        for p in parts
    ]
    free = [f"{'<free>':<16} offset=0x{o:06X} size=0x{s:06X} ({_human(s)})" for o, s in free_ranges(parts)]
    total = sum(s for _, s in free_ranges(parts))
    return ("== Partition table ==", *rows, *free, f"Flash size: 8.0 MB, free total: {_human(total)}")


def _align(n: int, a: int = 0x10000) -> int:
    return (n + a - 1) // a * a


class FakeLauncher:
    """A serial.Serial stand-in for the Launcher console (or a silent firmware when responsive=False)."""

    def __init__(self, clock: FakeClock, *, parts: tuple[Part, ...] | None = None, version: str = "2.9.1",
                 responsive: bool = True, banner_until: float | None = None, ack_step: int = CHUNK,
                 fail_at: int | None = None, stall_at: int | None = None, say_ok: bool = True,
                 reboot: bool = True, no_space: bool = False, timeout: float = 0.2) -> None:  # fmt: skip
        self.clock, self.timeout, self.version = clock, timeout, version
        self.parts = parts if parts is not None else standard_table(boot="cpav02")
        self.responsive, self.banner_until, self.next_banner = responsive, banner_until, clock()
        self.ack_step, self.fail_at, self.stall_at = ack_step, fail_at, stall_at
        self.say_ok, self.reboot, self.no_space = say_ok, reboot, no_space
        self.out, self.line, self.payload = bytearray(), bytearray(), bytearray()
        self.commands: list[str] = []
        self.expect, self.acked, self.name = 0, 0, ""
        self.lost = self.lose_when_drained = self.closed = False
        self.reset_input_calls = 0

    # -- serial.Serial surface ------------------------------------------------------------------
    @property
    def in_waiting(self) -> int:
        self._check()
        self._banners()
        return len(self.out)

    def read(self, n: int = 1) -> bytes:
        self._check()
        self._banners()
        if not self.out:
            self.clock.sleep(self.timeout)
            self._banners()
        data = bytes(self.out[:n])
        del self.out[:n]
        if self.lose_when_drained and not self.out:
            self.lost = True
        return data

    def write(self, data: bytes) -> int:
        self._check()
        for byte in data:
            self._payload_byte(byte) if self.expect else self._line_byte(byte)
        return len(data)

    def flush(self) -> None:
        self._check()

    def open(self) -> None:
        self._check()

    def reset_input_buffer(self) -> None:
        self.reset_input_calls += 1
        self.out.clear()

    def close(self) -> None:
        self.closed = True

    # -- device behaviour -----------------------------------------------------------------------
    def _check(self) -> None:
        if self.lost:
            raise serial.SerialException("device reports readiness to read but returned no data")

    def _emit(self, text: str) -> None:
        self.out += (text + "\r\n").encode()

    def _banners(self) -> None:
        while self.banner_until is not None and self.next_banner <= min(self.clock(), self.banner_until):
            self._emit(BANNER)
            self.next_banner += 0.5

    def _line_byte(self, byte: int) -> None:
        if byte != 0x0A:
            self.line.append(byte)
            return
        text = self.line.decode("utf-8", "replace").strip()
        self.line.clear()
        if text and self.responsive:
            self.commands.append(text)
            self._command(text.split())

    def _command(self, tokens: list[str]) -> None:
        if tokens == ["version"]:
            self._emit(f"Launcher {self.version}")
        elif tokens == ["partitions"]:
            for row in render_table(self.parts):
                self._emit(row)
        elif tokens[:2] == ["flash", "firmware"] and len(tokens) >= 4:
            self._start_flash(tokens[2], int(tokens[3]))
        else:
            self._emit("ERR unknown command, type 'help' for command list")

    def _start_flash(self, name: str, size: int) -> None:
        if self.no_space:
            self._emit("ERR no space for app: not enough free flash")
            return
        self.name, self.expect, self.acked = name, size, 0
        self.payload.clear()
        self._emit(f"READY {size}")

    def _payload_byte(self, byte: int) -> None:
        self.payload.append(byte)
        got = len(self.payload)
        if self.fail_at is not None and got >= self.fail_at:
            self._emit("ERR Write failed")
            self.expect = 0
            return
        if self.stall_at is not None and got >= self.stall_at:
            return
        if got - self.acked >= self.ack_step or got == self.expect:
            self.acked = got
            self._emit(f"ACK {got}/{self.expect}")
        if got == self.expect:
            self._finish()

    def _finish(self) -> None:
        size, self.expect = self.expect, 0
        apps = [p for p in self.parts if p.subtype.startswith("ota_")]
        new = Part(self._next_label(), "app", f"ota_{len(apps)}", free_ranges(self.parts)[0][0], _align(size), True)
        self.parts = (*(replace(p, boot=False) for p in self.parts), new)
        if self.say_ok:
            self._emit("")
            self._emit("OK flashed, rebooting")
        if self.reboot:
            self.lose_when_drained = bool(self.out)
            self.lost = not self.out

    def _next_label(self) -> str:
        base = "".join(c for c in self.name.lower() if c.isascii() and c.isalnum())[:6].ljust(6, "0")
        taken = {p.label for p in self.parts}
        free = (f"{base[:5]}{i}" for i in range(1, 10) if f"{base[:5]}{i}" not in taken)
        return base if base not in taken else next(free)


class FakeRun:
    """subprocess.run stand-in for `cpa`, `lsof` and `pio`: records argv, answers from a script."""

    def __init__(self, *, status: list[dict] | None = None, lsof: list[str] | None = None,
                 pio_code: int = 0, pause_code: int = 0) -> None:  # fmt: skip
        self.calls: list[list[str]] = []
        self.status = list(status or [{"running": True, "paused_s": 0}])
        self.lsof = list(lsof or [""])
        self.pio_code, self.pause_code = pio_code, pause_code

    def __call__(self, argv: list[str], **_: object) -> subprocess.CompletedProcess:
        self.calls.append(list(argv))
        tool, args = argv[0].rsplit("/", 1)[-1], argv[1:]
        if tool == "cpa" and args[:1] == ["status"]:
            doc = self.status.pop(0) if len(self.status) > 1 else self.status[0]
            return subprocess.CompletedProcess(argv, 0, json.dumps(doc), "")
        if tool == "cpa":
            return subprocess.CompletedProcess(argv, self.pause_code, "paused", "")
        if tool == "lsof":
            out = self.lsof.pop(0) if len(self.lsof) > 1 else self.lsof[0]
            return subprocess.CompletedProcess(argv, 0 if out else 1, out, "")
        return subprocess.CompletedProcess(argv, self.pio_code, "", "")

    def made(self, *prefix: str) -> list[list[str]]:
        return [c for c in self.calls if c[1 : 1 + len(prefix)] == list(prefix)]
