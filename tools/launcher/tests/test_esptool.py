"""Write the verified Launcher image at 0x0 with PlatformIO's esptool (no G0: USB-JTAG auto-download)."""

from pathlib import Path

from cpalauncher.esptool import find_pio, flash_command, write_flash
from fakes import FakeRun


def test_flash_command_uses_platformio_esptool_at_0x0() -> None:
    cmd = flash_command("/u/pio", "/dev/cu.usbmodem1101", Path("/c/2.9.1/Launcher-m5stack-cardputer.bin"))
    assert cmd == [
        "/u/pio", "pkg", "exec", "--package", "tool-esptoolpy", "--",
        "esptool.py", "--chip", "esp32s3", "--port", "/dev/cu.usbmodem1101", "--baud", "460800",
        "write_flash", "0x0", "/c/2.9.1/Launcher-m5stack-cardputer.bin",
    ]  # fmt: skip


def test_find_pio_prefers_path_then_the_uv_tool(tmp_path: Path) -> None:
    assert find_pio(lambda _n: "/opt/pio", tmp_path) == "/opt/pio"
    assert find_pio(lambda _n: None, tmp_path) is None
    local = tmp_path / ".local" / "bin" / "pio"
    local.parent.mkdir(parents=True)
    local.write_text("#!/bin/sh\n")
    assert find_pio(lambda _n: None, tmp_path) == str(local)


def test_write_flash_returns_the_exit_code() -> None:
    run = FakeRun(pio_code=2)
    assert write_flash(["/u/pio", "pkg"], run) == 2
    assert run.calls == [["/u/pio", "pkg"]]


def test_write_flash_when_pio_cannot_start() -> None:
    def missing(*_a, **_k):
        raise FileNotFoundError("pio")

    assert write_flash(["/u/pio"], missing) == 127
