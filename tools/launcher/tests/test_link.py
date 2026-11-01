"""Serial shell: safe-open (03 §2 pit 3), line splitting, the reader, port discovery, lsof."""

import pytest
import serial

from cpalauncher.link import (
    Console,
    PortInfo,
    PortLost,
    busy_pids,
    find_port,
    parse_lsof,
    safe_open,
    split_lines,
)
from fakes import BANNER, FakeClock, FakeLauncher, FakeRun

# ---- split_lines (pure) -------------------------------------------------------------------------


def test_split_lines_handles_crlf_and_keeps_the_tail() -> None:
    assert split_lines(b"Launcher 2.9.1\r\nACK 20") == (("Launcher 2.9.1",), b"ACK 20")


def test_split_lines_drops_empty_lines_and_replaces_bad_utf8() -> None:
    lines, rest = split_lines(b"\r\n\xff\xfeOK\n\n")
    assert (lines, rest) == (("��OK",), b"")


def test_split_lines_caps_a_runaway_line() -> None:
    lines, rest = split_lines(b"x" * 9000, cap=8192)
    assert (lines, rest) == ((), b"")


# ---- safe_open ----------------------------------------------------------------------------------


class Recorder:
    """Records every control-line state the port goes through."""

    def __init__(self, fail_rts: bool = False) -> None:
        self.events: list[tuple[str, object]] = []
        self.is_open, self.fail_rts, self._rts, self._dtr = False, fail_rts, True, True

    def __setattr__(self, key, value):
        if key in ("port", "baudrate", "timeout", "write_timeout", "exclusive", "dsrdtr"):
            self.events.append((key, value))
        object.__setattr__(self, key, value)

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        if self.fail_rts:
            raise serial.SerialException("ioctl failed")
        object.__setattr__(self, "_rts", value)
        self.events.append(("lines", (self._rts, self._dtr)))

    @property
    def dtr(self):
        return self._dtr

    @dtr.setter
    def dtr(self, value):
        object.__setattr__(self, "_dtr", value)
        self.events.append(("lines", (self._rts, self._dtr)))

    def open(self):
        self.events.append(("open", None))
        object.__setattr__(self, "is_open", True)

    def close(self):
        self.events.append(("close", None))


def test_safe_open_order_never_resets_the_chip() -> None:
    rec = Recorder()
    port = safe_open("/dev/cu.usbmodem1101", factory=lambda: rec)
    assert port is rec
    names = [name for name, _ in rec.events]
    assert names.index("dsrdtr") < names.index("open")
    assert ("dsrdtr", True) in rec.events and ("exclusive", True) in rec.events
    lines = [value for name, value in rec.events if name == "lines"]
    assert lines == [(False, True), (False, False)]  # (RTS, DTR): set download flag, then clear it
    assert (True, False) not in lines


def test_safe_open_closes_when_the_lines_cannot_be_set() -> None:
    rec = Recorder(fail_rts=True)
    with pytest.raises(serial.SerialException):
        safe_open("/dev/x", factory=lambda: rec)
    assert rec.events[-1] == ("close", None)


# ---- ports and lsof -----------------------------------------------------------------------------


def test_find_port_takes_the_first_espressif_usb_jtag_port() -> None:
    ports = (
        PortInfo("/dev/cu.usbserial-1", 0x10C4, 0xEA60),
        PortInfo("/dev/cu.usbmodem2101", 0x303A, 0x1001),
        PortInfo("/dev/cu.usbmodem1101", 0x303A, 0x1001),
        PortInfo("/dev/cu.Bluetooth", None, None),
    )
    assert find_port(ports) == "/dev/cu.usbmodem1101"
    assert find_port(ports[:1]) is None


def test_parse_lsof_lists_other_processes() -> None:
    assert parse_lsof("123\n456\n\nnot-a-pid\n", own_pid=456) == (123,)
    assert parse_lsof("", own_pid=1) == ()


def test_busy_pids_runs_lsof_on_the_device() -> None:
    run = FakeRun(lsof=["777\n"])
    assert busy_pids("/dev/cu.usbmodem1101", run=run, own_pid=1) == (777,)
    assert run.calls == [["lsof", "-t", "/dev/cu.usbmodem1101"]]


def test_busy_pids_is_empty_when_lsof_is_missing() -> None:
    def missing(*_a, **_k):
        raise FileNotFoundError("lsof")

    assert busy_pids("/dev/x", run=missing) == ()


# ---- Console over the fake Launcher -------------------------------------------------------------


def test_send_and_poll_a_reply() -> None:
    clock = FakeClock()
    console = Console(FakeLauncher(clock), clock)
    console.send("version")
    assert console.poll(1.0) == ("Launcher 2.9.1",)


def test_poll_times_out_with_nothing() -> None:
    clock = FakeClock()
    console = Console(FakeLauncher(clock, responsive=False), clock)
    console.send("version")
    t0 = clock()
    assert console.poll(1.0) == ()
    assert 1.0 <= clock() - t0 < 1.5


def test_collect_reads_until_the_stop_line() -> None:
    clock = FakeClock()
    console = Console(FakeLauncher(clock), clock)
    console.send("partitions")
    lines = console.collect(5.0, stop=lambda line: line.startswith("Flash size:"))
    assert lines[0] == "== Partition table ==" and lines[-1].startswith("Flash size:")
    assert clock() < 1001.0  # stopped early


def test_collect_gathers_banners_for_the_whole_window() -> None:
    clock = FakeClock()
    console = Console(FakeLauncher(clock, banner_until=clock() + 10), clock)
    lines = console.collect(1.2)
    assert lines.count(BANNER) == 3


def test_send_rejects_embedded_newlines() -> None:
    clock = FakeClock()
    with pytest.raises(ValueError):
        Console(FakeLauncher(clock), clock).send("version\nreboot")


@pytest.mark.parametrize("exc", [serial.SerialException("gone"), OSError(6, "Device not configured")])
def test_io_errors_become_port_lost(exc: Exception) -> None:
    class Broken:
        in_waiting = 0

        def read(self, n=1):
            raise exc

        def write(self, data):
            raise exc

        def reset_input_buffer(self):
            raise exc

    clock = FakeClock()
    console = Console(Broken(), clock)
    with pytest.raises(PortLost):
        console.poll(1.0)
    with pytest.raises(PortLost):
        console.send("version")
    with pytest.raises(PortLost):
        console.drain()


def test_drain_and_close() -> None:
    clock = FakeClock()
    fake = FakeLauncher(clock)
    console = Console(fake, clock)
    console.drain()
    console.close()
    console.close()
    assert fake.reset_input_calls == 1 and fake.closed


def test_close_swallows_errors() -> None:
    class Sticky:
        def close(self):
            raise OSError("busy")

    Console(Sticky(), FakeClock()).close()
