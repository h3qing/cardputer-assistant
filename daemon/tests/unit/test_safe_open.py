"""SafeOpen control-line sequence (03 §2 pit 3, US-04 AC4): (RTS, DTR) must never be (1, 0)."""

from __future__ import annotations

import pytest

from cpad.device import safe_open

RESET = (True, False)


class LineRecorder:
    """Models pyserial's posix Serial on macOS: opening raises both lines; dsrdtr=True leaves DTR alone."""

    def __init__(self):
        self.port = None
        self.baudrate = None
        self.timeout = None
        self.write_timeout = None
        self.exclusive = None
        self.dsrdtr = False
        self.is_open = False
        self._rts = True
        self._dtr = True
        self.lines = None
        self.history = []

    def _set(self, rts, dtr):
        self.lines = (rts, dtr)
        self.history.append(self.lines)

    def open(self):
        self._set(True, True)  # macOS asserts both lines when the port opens
        self.is_open = True
        dtr = self.lines[1] if self.dsrdtr else self._dtr
        self._set(self._rts, dtr)

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        self._rts = value
        if self.is_open:
            self._set(value, self.lines[1])

    @property
    def dtr(self):
        return self._dtr

    @dtr.setter
    def dtr(self, value):
        self._dtr = value
        if self.is_open:
            self._set(self.lines[0], value)

    def close(self):
        self.is_open = False


def test_safe_open_never_passes_through_reset():
    port = safe_open.safe_open("/dev/cu.usbmodem1101", factory=LineRecorder)
    assert RESET not in port.history
    assert port.lines == (False, False)
    assert port.history == [(True, True), (True, True), (False, True), (False, False)]
    assert (port.dsrdtr, port.exclusive, port.port) == (True, True, "/dev/cu.usbmodem1101")


def test_the_recorder_does_catch_the_wrong_order():
    port = LineRecorder()
    port.open()
    port.dtr = False
    assert RESET in port.history


def test_failure_after_open_closes_the_port():
    class Broken(LineRecorder):
        @LineRecorder.rts.setter
        def rts(self, value):
            raise OSError(25, "Inappropriate ioctl for device")

    made = []

    def factory():
        made.append(Broken())
        return made[-1]

    with pytest.raises(OSError):
        safe_open.safe_open("/dev/x", factory=factory)
    assert not made[0].is_open
