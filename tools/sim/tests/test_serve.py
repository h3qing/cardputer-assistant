"""`cpa-sim serve` on its own, through the pty with a bare host: the protocol, stdin steps, shots, reboot.

Needs the built simulator (`pio run -d firmware/sim`, or $CPA_SIM_BIN).
"""

import json
import subprocess
import time

import pytest

from simtools.png_read import read_png
from simtools.serial_pty import PtySerial
from simtools.serve import SimError, SimServe, sim_bin

pytestmark = pytest.mark.usefixtures("built_sim")  # conftest.py: skipped without a build, failed if stale

HELLO = {"v": 0, "t": "hello", "id": "h1", "role": "host", "app": "cpad/test", "agent": "ok",
         "caps": {"spawn": True, "reply": True}}  # fmt: skip


class Host:
    """A bare host on the pty: writes JSON lines, reads the device's replies."""

    def __init__(self, path: str):
        self.port = PtySerial()
        self.port.port, self.port.timeout, self.port.dsrdtr = path, 0.05, True
        self.port.open()
        self.buf = b""
        self.n = 1

    def request(self, msg: dict) -> dict:
        self.n += 1
        wire = {"v": 0, "id": f"h{self.n}", **msg}
        self.port.write((json.dumps(wire) + "\n").encode())
        return self.reply_to(wire["id"])

    def reply_to(self, msg_id: str, timeout: float = 3.0) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.buf += self.port.read(4096)
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                msg = json.loads(line) if line.strip() else {}
                if msg.get("re") == msg_id:
                    return msg
        raise AssertionError(f"no reply to {msg_id}")

    def hello(self) -> dict:
        self.port.write((json.dumps(HELLO) + "\n").encode())
        return self.reply_to("h1")


@pytest.fixture
def sim(tmp_path):
    serve = SimServe.start(tmp_path / "shots")
    try:
        yield serve
    finally:
        assert serve.stop() == 0


def test_handshake_and_debug_state(sim):
    host = Host(sim.path)
    try:
        hello = host.hello()
        assert hello["t"] == "hello" and hello["dev"] == "100000000051" and hello["store"] == "sd"
        state = host.request({"t": "debug.state"})
        assert state["link"] == "Ready" and state["mod"] == "home"  # v0.4.0: the root page
    finally:
        host.port.close()


def test_stdin_keys_and_a_shot(sim):
    host = Host(sim.path)
    try:
        host.hello()
        sim.send("press g0", "type hi")
        shot = sim.shot("compose")
        state = host.request({"t": "debug.state"})
        assert state["overlay"] == "compose"
        img = read_png(shot.path)
        assert (img.width, img.height) == (480, 270)
        assert shot.backlight == "on"
    finally:
        host.port.close()


def test_refused_steps_say_why(sim):
    sim.send("host ping")
    with pytest.raises(SimError, match="only works in a script"):
        sim.expect("never ", 3.0)


def test_debug_reboot_restarts_with_the_next_boot(sim):
    host = Host(sim.path)
    try:
        first = host.hello()
        assert host.request({"t": "debug.reboot"})["t"] == "ack"
        sim.expect("reboot 2", 3.0)
        again = host.hello()
        assert again["boot"] != first["boot"] and again["rst"] == "software"
    finally:
        host.port.close()


def test_a_host_that_overruns_rx_loses_bytes(sim):
    """As on the device, input beyond the 8 KB RX queue is lost, not pushed back on the host: serve says how much."""
    host = Host(sim.path)
    try:
        host.port.write_timeout = 5
        host.port.write((b"x" * 63 + b"\n") * 512)  # 32 KB at once; the device reads 4 lines per loop
        lost = int(sim.expect("rx_dropped ", 3.0).split(" ")[1])
        assert 0 < lost < 32 * 1024
        time.sleep(1.0)  # what fitted is read
        host.port.write(b"\n")  # ends a line cut short by the loss
        assert host.hello()["t"] == "hello"
    finally:
        host.port.close()


def test_usb_off_and_battery(sim):
    sim.send("usb off", "bat 3300", "usb on")
    assert sim.expect("usb ", 3.0) == "usb off"
    assert sim.expect("bat ", 3.0) == "bat 3300"
    assert sim.expect("usb ", 3.0) == "usb on"


def test_the_end_of_stdin_stops_it(tmp_path):
    done = subprocess.run([str(sim_bin()), "serve", "--shots", str(tmp_path)], input="", capture_output=True,
                          text=True, timeout=10, check=False)  # fmt: skip
    lines = done.stdout.splitlines()
    assert done.returncode == 0 and lines[0].startswith("pty /dev/") and lines[-1] == "bye"


def test_bad_options_and_mic_files(tmp_path):
    bad = subprocess.run([str(sim_bin()), "serve", "--store", "floppy"], capture_output=True, text=True, timeout=10,
                         check=False)  # fmt: skip
    assert bad.returncode == 2 and "usage:" in bad.stderr
    wav = tmp_path / "x.wav"
    wav.write_bytes(b"RIFF\x04\x00\x00\x00WAVE")
    refused = subprocess.run([str(sim_bin()), "serve", "--mic", str(wav)], capture_output=True, text=True,
                             timeout=10, check=False)  # fmt: skip
    assert refused.returncode == 2 and "no fmt chunk" in refused.stderr
