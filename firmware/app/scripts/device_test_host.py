#!/usr/bin/env python3
"""Host side of the on-device test suites (06 WP4: pio test -e cardputer-adv-test -f 'device/*').

PlatformIO runs this as `test_testing_command` after uploading a suite and parses
what it prints as Unity output. It replaces PlatformIO's own serial reader because:
  - the port must be opened the safe way (03 §2): dsrdtr=True, then RTS=0, then DTR=0,
    so (RTS, DTR) never reads (1, 0), which would reset the chip;
  - core 2.0.17 can park device TX until the host writes (03 §3, ADR-0001 §5.2): after
    500 ms of silence a blank line is sent as a nudge (the device ignores it);
  - the RX test needs a host that sends an 8 KB burst when the device says
    CPA_RX_READY, and stays quiet until CPA_RX_DONE;
  - a crash reboots the device, which runs the suite again (docs/07 §6.7 #1): the
    panic and its backtrace are printed, then the suite fails at "Rebooting...".
"""

from __future__ import annotations

import glob as globmod
import os
import re
import sys
import time

BURST_BYTES = 8192
NUDGE_AFTER_S = 0.5
DEADLINE_S = 420
PORT_WAIT_S = 30
_SUMMARY = re.compile(r"^\s*\d+ Tests \d+ Failures \d+ Ignored\s*$")


def burst_pattern(n: int = BURST_BYTES) -> bytes:
    return bytes(ord("A") + i % 26 for i in range(n))


def is_summary(line: str) -> bool:
    return bool(_SUMMARY.match(line))


def is_reboot(line: str) -> bool:
    return line.startswith("Rebooting...")  # ESP-IDF's panic handler, last line


def should_nudge(silent_s: float, in_burst: bool) -> bool:
    return not in_burst and silent_s >= NUDGE_AFTER_S


def find_port(glob=globmod.glob) -> str | None:
    if os.environ.get("CPA_TEST_PORT"):
        return os.environ["CPA_TEST_PORT"]
    ports = sorted(glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def safe_open(port: str, factory=None):
    """03 §2: open with dsrdtr=True (DTR untouched), then RTS=0 (sets the download flag
    only, no reset), then DTR=0 (clears it). Closing later drops lines already at 0."""
    if factory is None:
        import serial  # imported here so the pure helpers test without pyserial

        factory = serial.Serial
    s = factory()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.05
    s.dsrdtr = True
    s.open()
    s.rts = False
    s.dtr = False
    return s


def wait_for_port() -> str | None:
    time.sleep(1.5)  # the upload's hard reset re-enumerates the USB device
    end = time.monotonic() + PORT_WAIT_S
    while time.monotonic() < end:
        port = find_port()
        if port and os.path.exists(port):
            return port
        time.sleep(0.2)
    return None


def run(port: str, factory=None) -> int:
    s = safe_open(port, factory)
    s.write(b"\n")  # start signal: the suite waits for the host before it runs
    buf, in_burst, last_rx, done_at = b"", False, time.monotonic(), None
    end = time.monotonic() + DEADLINE_S
    while time.monotonic() < end:
        chunk = s.read(512)
        now = time.monotonic()
        if chunk:
            last_rx = now
            buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            print(line, flush=True)
            if line.startswith("CPA_RX_READY"):
                in_burst = True
                s.write(burst_pattern())
            elif line.startswith("CPA_RX_DONE"):
                in_burst = False
            elif is_summary(line):
                done_at = now
            elif is_reboot(line):
                s.close()
                print("device_test_host: the device crashed and rebooted", flush=True)
                return 1
        if done_at is not None and now - done_at > 0.5:
            s.close()
            return 0
        if should_nudge(now - last_rx, in_burst):
            s.write(b"\n")
            last_rx = now
    s.close()
    print("device_test_host: timed out waiting for the Unity summary", flush=True)
    return 1


def main() -> int:
    port = wait_for_port()
    if not port:
        print(
            "device_test_host: no /dev/cu.usbmodem* port (is the board plugged in?)",
            flush=True,
        )
        return 1
    return run(port)


if __name__ == "__main__":
    sys.exit(main())
