"""PtySerial and the sim port lister, on a real pty: cpad's SafeOpen and PortFinder accept them unchanged."""

import os
import time

import pytest
import serial
from cpad.device.port_finder import Backoff, choose
from cpad.device.safe_open import safe_open

from simtools.serial_pty import SIM_SERIAL, PtySerial, sim_lister


@pytest.fixture
def pty():
    master, slave = os.openpty()
    path = os.ttyname(slave)
    os.close(slave)
    try:
        yield master, path
    finally:
        os.close(master)


def test_plain_pyserial_cannot_open_a_pty_safely(pty):
    _, path = pty
    with pytest.raises(OSError):
        safe_open(path, serial.Serial)


def test_safe_open_works_with_pty_serial(pty):
    master, path = pty
    port = safe_open(path, PtySerial)
    try:
        os.write(master, b'{"t":"x"}\n')
        time.sleep(0.02)
        assert port.read(port.in_waiting) == b'{"t":"x"}\n'
        port.write(b"host\n")
        time.sleep(0.02)
        assert os.read(master, 100) == b"host\n"  # raw: no echo, no CR added
    finally:
        port.close()


def test_the_port_is_exclusive(pty):
    _, path = pty
    first = safe_open(path, PtySerial)
    try:
        with pytest.raises(serial.SerialException):
            safe_open(path, PtySerial)
    finally:
        first.close()


def test_the_lister_shows_only_the_sim_as_a_cardputer(pty):
    _, path = pty
    ports = sim_lister(path)()
    assert len(ports) == 1 and ports[0].device == path and ports[0].serial == SIM_SERIAL
    assert choose(ports, None, Backoff(), 0.0) == ports[0]
    assert choose(ports, SIM_SERIAL, Backoff(), 0.0) == ports[0]
    assert choose(ports, "some-real-board", Backoff(), 0.0) is None
