"""End-to-end CLI flows against the fake Launcher, a fake `cpa`/`lsof`/`pio` and a fake network."""

import hashlib
import io
import os
import struct
from dataclasses import replace
from pathlib import Path

import pytest

from cpa_launcher import main
from cpalauncher.flows import Deps
from cpalauncher.images import Known
from cpalauncher.link import PortInfo
from cpalauncher.release import Release, cache_path
from fakes import FakeClock, FakeLauncher, FakeRun

CARDPUTER = PortInfo("/dev/cu.usbmodem1101", 0x303A, 0x1001)
BLOB = os.urandom(3000)
LAUNCHER = Release("2.9.1", "https://example.invalid/L.bin", "L.bin", len(BLOB), hashlib.sha256(BLOB).hexdigest())


def app_image(size: int = 9000) -> bytes:
    header = bytes([0xE9, 3, 2, 0x4F]) + struct.pack("<I", 0x40380000) + bytes(4) + struct.pack("<H", 9)
    header += bytes(9) + b"\x01"
    return header + os.urandom(size - len(header))


class Device:
    """The Cardputer across reboots: once the port drops, the next open finds the Launcher menu again."""

    def __init__(self, clock: FakeClock, **kw) -> None:
        self.clock, self.kw, self.sessions = clock, kw, [FakeLauncher(clock, **kw)]

    @property
    def now(self) -> FakeLauncher:
        return self.sessions[-1]

    def __call__(self) -> FakeLauncher:
        if self.now.lost:
            keep = {k: v for k, v in self.kw.items() if k not in ("say_ok", "fail_at", "stall_at", "no_space")}
            self.sessions.append(FakeLauncher(self.clock, **{**keep, "parts": self.now.parts}))
        return self.now


class Env:
    def __init__(self, tmp_path: Path, *, run: FakeRun | None = None, ports=(CARDPUTER,), pio=True, **device) -> None:
        self.clock, self.out, self.tmp = FakeClock(0.0), [], tmp_path
        self.device = Device(self.clock, **device)
        self.run = run or FakeRun()
        factory = tmp_path / "backups" / "factory.bin"
        factory.parent.mkdir(exist_ok=True)
        self.factory_data = app_image(12_000)
        factory.write_bytes(self.factory_data)
        known = {"factory": Known("factory", "backups/factory.bin", "factory-v0.9", len(self.factory_data),
                                  hashlib.sha256(self.factory_data).hexdigest())}  # fmt: skip
        self.deps = Deps(
            say=self.out.append, clock=self.clock, sleep=self.clock.sleep, run=self.run,
            which=lambda n: f"/u/bin/{n}" if n == "cpa" or (n == "pio" and pio) else None,
            ports=lambda: ports, serial_factory=self.device, urlopen=lambda url, timeout: io.BytesIO(BLOB),
            home=tmp_path, env={"XDG_CACHE_HOME": str(tmp_path / "cache")}, repo_root=tmp_path,
            known=known, launcher=LAUNCHER,
        )  # fmt: skip

    def __call__(self, *argv: str) -> int:
        return main(list(argv), self.deps)

    def said(self, text: str) -> bool:
        return any(text in line for line in self.out)


def write_app(tmp_path: Path, name: str = "cpa-v0.3.0-app.bin", size: int = 9000) -> tuple[Path, bytes]:
    data = app_image(size)
    path = tmp_path / name
    path.write_bytes(data)
    return path, data


# ---- install --------------------------------------------------------------------------------------


def test_install_an_app_image(tmp_path: Path) -> None:
    env = Env(tmp_path)
    path, data = write_app(tmp_path)
    assert env("install", str(path)) == 0
    assert bytes(env.device.sessions[0].payload) == data
    assert "flash firmware cpa-v0.3.0 9000" in env.device.sessions[0].commands
    assert env.said("installed") and env.said("100%")
    pauses = env.run.made("pause")
    assert pauses[0][:2] == ["/u/bin/cpa", "pause"] and pauses[-1] == ["/u/bin/cpa", "pause", "1"]


def test_install_the_factory_app_by_name(tmp_path: Path) -> None:
    env = Env(tmp_path)
    assert env("install", "factory") == 0
    assert bytes(env.device.sessions[0].payload) == env.factory_data
    assert any(c.startswith("flash firmware factory-v0.9 ") for c in env.device.sessions[0].commands)


def test_install_with_a_lost_ok_verifies_through_the_daemon(tmp_path: Path) -> None:
    ready = {"running": True, "link": "ready", "device": {"fw": "cpa-fw/0.3.0", "launcher": True, "store": "sd"}}
    env = Env(tmp_path, run=FakeRun(status=[{"running": True, "paused_s": 0}, {"link": "offline"}, ready]),
              say_ok=False)  # fmt: skip
    path, _ = write_app(tmp_path)
    assert env("install", str(path)) == 0
    assert env.said("unconfirmed") and env.said("verified: link ready fw=cpa-fw/0.3.0")


def test_install_with_a_lost_ok_verifies_by_partitions_for_other_firmware(tmp_path: Path) -> None:
    env = Env(tmp_path, say_ok=False)
    assert env("install", "factory") == 0
    assert len(env.device.sessions) == 2 and env.device.sessions[1].commands[-1] == "partitions"
    assert env.said("press RST") and env.said("verified: facto1")


def test_install_explicit_daemon_verify_that_never_links_is_not_verified(tmp_path: Path) -> None:
    env = Env(tmp_path, run=FakeRun(status=[{"running": True, "paused_s": 0}, {"running": True, "link": "offline"}]))
    path, _ = write_app(tmp_path)
    assert env("install", str(path), "--verify", "daemon", "--verify-timeout", "10") == 3
    assert env.said("not verified: link offline")


def test_install_device_error_fails(tmp_path: Path) -> None:
    env = Env(tmp_path, no_space=True)
    path, _ = write_app(tmp_path)
    assert env("install", str(path)) == 1
    assert env.said("ERR no space")


def test_install_unplugged_mid_stream_fails_with_a_hint(tmp_path: Path) -> None:
    env = Env(tmp_path, stall_at=4096)
    path, _ = write_app(tmp_path)
    assert env("install", str(path)) == 1
    assert env.said("timeout")


def test_install_refuses_a_merged_image(tmp_path: Path) -> None:
    env = Env(tmp_path)
    data = bytearray(app_image(0x9000))
    data[0x8000:0x8002] = b"\xaa\x50"
    (tmp_path / "cpa-v0.3.0.bin").write_bytes(bytes(data))
    assert env("install", str(tmp_path / "cpa-v0.3.0.bin")) == 2
    assert env.run.made("pause") == [] and env.said("-app.bin")


def test_install_gives_up_when_the_launcher_never_answers(tmp_path: Path) -> None:
    env = Env(tmp_path, responsive=False)
    path, _ = write_app(tmp_path)
    assert env("install", str(path), "--timeout", "10") == 4
    assert env.said("needs power cycle") and env.run.made("pause")[-1][-1] == "1"


def test_install_stay_paused(tmp_path: Path) -> None:
    env = Env(tmp_path)
    path, _ = write_app(tmp_path)
    assert env("--stay-paused", "install", str(path)) == 0
    assert all(call[-1] != "1" for call in env.run.made("pause"))


def test_install_refuses_when_the_table_cannot_be_read(tmp_path: Path) -> None:
    class NoTable(FakeLauncher):
        def _command(self, tokens):
            if tokens == ["partitions"]:
                self._emit("ERR partition table read failed")
            else:
                super()._command(tokens)

    env = Env(tmp_path)
    env.device.sessions[0] = NoTable(env.clock)
    path, _ = write_app(tmp_path)
    assert env("install", str(path)) == 1
    assert not env.device.sessions[0].payload


# ---- other commands -------------------------------------------------------------------------------


def test_fetch_downloads_and_caches(tmp_path: Path) -> None:
    env = Env(tmp_path)
    assert env("fetch") == 0
    assert cache_path(tmp_path / "cache" / "cardputer-assistant" / "launcher", LAUNCHER).read_bytes() == BLOB


def test_fetch_failure(tmp_path: Path) -> None:
    env = Env(tmp_path)
    env.deps = replace(env.deps, urlopen=lambda url, timeout: io.BytesIO(b"bad"))
    assert env("fetch") == 2 and env.said("error:")


def test_flash_launcher(tmp_path: Path) -> None:
    env = Env(tmp_path, banner_until=5.0, parts=None)
    assert env("flash-launcher") == 0
    flash = [c for c in env.run.calls if c[0] == "/u/bin/pio"]
    assert flash and flash[0][-2:] == ["0x0", str(cache_path(tmp_path / "cache" / "cardputer-assistant" / "launcher",
                                                             LAUNCHER))]  # fmt: skip
    assert env.said("Launcher 2.9.1 is running")


def test_flash_launcher_esptool_failure(tmp_path: Path) -> None:
    env = Env(tmp_path, run=FakeRun(pio_code=2))
    assert env("flash-launcher") == 1 and env.said("needs power cycle")


def test_flash_launcher_needs_pio(tmp_path: Path) -> None:
    env = Env(tmp_path, pio=False)
    assert env("flash-launcher") == 2 and env.said("pio not found")


def test_flash_launcher_needs_a_port(tmp_path: Path) -> None:
    env = Env(tmp_path, ports=())
    assert env("flash-launcher") == 2 and env.said("no Cardputer")


def test_flash_launcher_refuses_a_held_port(tmp_path: Path) -> None:
    env = Env(tmp_path, run=FakeRun(lsof=["4242\n"]))
    assert env("flash-launcher") == 2 and env.said("4242")
    assert not [c for c in env.run.calls if c[0] == "/u/bin/pio"]


def test_wait(tmp_path: Path) -> None:
    env = Env(tmp_path)
    assert env("wait") == 0 and env.said("Launcher 2.9.1 menu is open")


def test_cmd_partitions(tmp_path: Path) -> None:
    env = Env(tmp_path)
    assert env("cmd", "partitions") == 0
    assert env.said("== Partition table ==") and env.said("<-- BOOT")


def test_cmd_without_a_reply(tmp_path: Path) -> None:
    env = Env(tmp_path, responsive=False)
    assert env("cmd", "version") == 4


def test_cmd_refuses_anything_that_is_not_read_only(tmp_path: Path) -> None:
    env = Env(tmp_path)
    assert env("cmd", "wifi", "list") == 2
    assert env.device.sessions[0].commands == []


def test_interrupt_resumes_the_daemon(tmp_path: Path) -> None:
    env = Env(tmp_path)

    def boom():
        raise KeyboardInterrupt

    env.deps = replace(env.deps, ports=boom)
    assert env("wait") == 130
    assert env.run.made("pause")[-1] == ["/u/bin/cpa", "pause", "1"]


def test_usage_errors_exit_2(tmp_path: Path) -> None:
    with pytest.raises(SystemExit) as exc:
        Env(tmp_path)("install")
    assert exc.value.code == 2
