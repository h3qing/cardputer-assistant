"""Tests for the firmware build scripts (run: uv run --with pytest --with pyserial==3.5 pytest firmware/app/scripts -q)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(
    0, str(Path(__file__).resolve().parents[1] / "test" / "core" / "support")
)

import check_budget as budget
import check_partitions as parts
import device_test_host as host
import make_image
import pre_upload
import stack_budget

MAP_ONE_LINE = """
Linker script and memory map

.dram0.data     0x000000003fc907a0     0x4e60
                0x000000003fc907a0                _data_start = ABSOLUTE (.)
 .data          0x000000003fc907a0       0x10 foo.o
.noinit         0x000000003fc95600        0x0
.dram0.bss      0x000000003fc95600     0x2c40
"""

MAP_TWO_LINES = """
.dram0.data
                0x000000003fc907a0     0x100
.dram0.bss
                0x000000003fc95600     0x200
.noinit         0x000000003fc95600       0x10
"""


# ---- check_budget -------------------------------------------------------------------


def test_static_ram_sums_data_bss_noinit() -> None:
    assert budget.static_ram(MAP_ONE_LINE) == {
        ".dram0.data": 0x4E60,
        ".noinit": 0,
        ".dram0.bss": 0x2C40,
    }
    assert sum(budget.static_ram(MAP_TWO_LINES).values()) == 0x100 + 0x200 + 0x10


def test_static_ram_missing_section_is_an_error() -> None:
    with pytest.raises(ValueError):
        budget.static_ram(".dram0.data 0x1 0x2\n")


def write_build(tmp: Path, map_text: str, bin_size: int) -> Path:
    tmp.mkdir(parents=True, exist_ok=True)
    (tmp / "firmware.map").write_text(map_text)
    (tmp / "firmware.bin").write_bytes(b"\0" * bin_size)
    return tmp


def test_budget_main_passes_and_fails(tmp_path: Path) -> None:
    ok = write_build(tmp_path / "ok", MAP_ONE_LINE, 1000)
    assert budget.main([str(ok)]) == 0
    big_ram = MAP_ONE_LINE.replace("0x2c40", hex(budget.RAM_MAX))
    assert budget.main([str(write_build(tmp_path / "ram", big_ram, 1000))]) == 1
    assert (
        budget.main(
            [str(write_build(tmp_path / "app", MAP_ONE_LINE, budget.APP_MAX + 1))]
        )
        == 1
    )
    assert budget.main([str(tmp_path / "missing")]) == 1


def test_budget_limits_match_the_architecture() -> None:
    assert budget.RAM_MAX == 96 * 1024
    assert budget.APP_MAX == 2560 * 1024
    assert budget.APP_WARN == 1_835_008  # 1.75 MiB: room for Bruce 1.16.1 under the Launcher


def test_budget_warns_over_the_launcher_soft_limit(tmp_path: Path, capsys) -> None:
    at = write_build(tmp_path / "at", MAP_ONE_LINE, budget.APP_WARN)
    assert budget.main([str(at)]) == 0
    assert "WARN" not in capsys.readouterr().out
    over = write_build(tmp_path / "over", MAP_ONE_LINE, budget.APP_WARN + 1)
    assert budget.main([str(over)]) == 0  # a warning only: the exit code stays 0
    out = capsys.readouterr().out
    assert "WARN" in out and "1,835,009" in out
    hard = write_build(tmp_path / "hard", MAP_ONE_LINE, budget.APP_MAX + 1)
    assert budget.main([str(hard)]) == 1
    assert "FAIL" in capsys.readouterr().out


# ---- pre_upload ---------------------------------------------------------------------


class Ran:
    def __init__(self, code: int = 0, exc: Exception | None = None) -> None:
        self.code, self.exc, self.argv = code, exc, None

    def __call__(self, argv, **kwargs):
        self.argv = argv
        assert kwargs.get("timeout")  # never hang an upload
        if self.exc:
            raise self.exc
        return subprocess.CompletedProcess(argv, self.code, "", "")


def test_pre_upload_only_for_uploads() -> None:
    run = Ran()
    assert pre_upload.pause_daemon(
        ["buildprog"], which=lambda _: "/x/cpa", run=run
    ).startswith("skip")
    assert run.argv is None


def test_pre_upload_skips_without_cpa() -> None:
    assert "not found" in pre_upload.pause_daemon(
        ["upload"], which=lambda _: None, run=Ran()
    )


def test_pre_upload_pauses_for_120_seconds() -> None:
    run = Ran()
    assert (
        pre_upload.pause_daemon(["upload"], which=lambda _: "/x/cpa", run=run)
        == "paused"
    )
    assert run.argv == ["/x/cpa", "pause", "120"]


def test_pre_upload_never_fails_the_upload() -> None:
    assert pre_upload.pause_daemon(
        ["upload"], which=lambda _: "/x/cpa", run=Ran(code=3)
    ).startswith("warn")
    timeout = subprocess.TimeoutExpired("cpa", 10)
    assert pre_upload.pause_daemon(
        ["upload"], which=lambda _: "/x/cpa", run=Ran(exc=timeout)
    ).startswith("warn")
    assert pre_upload.pause_daemon(
        ["upload"], which=lambda _: "/x/cpa", run=Ran(exc=OSError())
    ).startswith("warn")


class Cpa:
    """Fake `cpa`: `status --json` prints `status`; every call's arguments are kept."""

    def __init__(self, status: str) -> None:
        self.status, self.calls = status, []

    def __call__(self, argv, **kwargs):
        assert kwargs.get("timeout")  # never hang an upload
        self.calls = [*self.calls, argv[1:]]
        out = self.status if argv[1] == "status" else ""
        return subprocess.CompletedProcess(argv, 0, out, "")


def status_json(**fields) -> str:
    return json.dumps({"ok": True, "running": True, "link": "paused", **fields})


def test_pre_upload_keeps_a_longer_pause() -> None:
    # 08 §4: an agent ran `cpa pause 5400`; `cpa pause 120` would cut that to 120 s.
    cpa = Cpa(status_json(paused_s=5390.4))
    msg = pre_upload.pause_daemon(["upload"], which=lambda _: "/x/cpa", run=cpa)
    assert msg.startswith("skip") and "5390" in msg
    assert cpa.calls == [["status", "--json"]]


def test_pre_upload_extends_a_shorter_pause() -> None:
    cpa = Cpa(status_json(paused_s=30.0))
    assert (
        pre_upload.pause_daemon(["upload"], which=lambda _: "/x/cpa", run=cpa)
        == "paused"
    )
    assert cpa.calls == [["status", "--json"], ["pause", "120"]]


@pytest.mark.parametrize(
    "out",
    [
        "",
        "not json",
        "[]",
        json.dumps({"running": False}),  # cpad not running: no paused_s
        status_json(paused_s=None),
        status_json(paused_s="soon"),
    ],
)
def test_pre_upload_pauses_when_the_pause_left_is_unknown(out: str) -> None:
    cpa = Cpa(out)
    assert (
        pre_upload.pause_daemon(["upload"], which=lambda _: "/x/cpa", run=cpa)
        == "paused"
    )
    assert cpa.calls[-1] == ["pause", "120"]


# ---- device_test_host ---------------------------------------------------------------


def test_burst_pattern_has_no_newlines_and_is_8k() -> None:
    data = host.burst_pattern()
    assert (
        len(data) == 8192
        and b"\n" not in data
        and data[:3] == b"ABC"
        and data[26] == ord("A")
    )


def test_unity_summary_detection() -> None:
    assert host.is_summary("3 Tests 0 Failures 0 Ignored")
    assert host.is_summary("12 Tests 1 Failures 0 Ignored ")
    assert not host.is_summary("test/device/x.cpp:4:t:PASS")


def test_nudge_policy() -> None:
    assert host.should_nudge(silent_s=0.6, in_burst=False)
    assert not host.should_nudge(silent_s=0.4, in_burst=False)
    assert not host.should_nudge(silent_s=5.0, in_burst=True)


class FakeSerial:
    """Records the control-line states as pyserial would set them."""

    def __init__(self) -> None:
        self.events: list[tuple[str, object]] = []
        self.dsrdtr = False
        self.port = self.baudrate = self.timeout = None
        self._rts = self._dtr = True

    def open(self) -> None:
        self.events.append(("open", (self._rts, self._dtr, self.dsrdtr)))

    @property
    def rts(self) -> bool:
        return self._rts

    @rts.setter
    def rts(self, v: bool) -> None:
        self._rts = v
        self.events.append(("lines", (self._rts, self._dtr)))

    @property
    def dtr(self) -> bool:
        return self._dtr

    @dtr.setter
    def dtr(self, v: bool) -> None:
        self._dtr = v
        self.events.append(("lines", (self._rts, self._dtr)))


def test_safe_open_never_drives_rts1_dtr0() -> None:
    s = FakeSerial()
    host.safe_open("/dev/cu.usbmodem1101", factory=lambda: s)
    assert s.events[0] == (
        "open",
        (True, True, True),
    )  # dsrdtr: pyserial leaves DTR alone
    states = [e[1] for e in s.events if e[0] == "lines"]
    assert states == [(False, True), (False, False)]
    assert (True, False) not in states


def test_reboot_detection() -> None:
    assert host.is_reboot("Rebooting...")
    assert not host.is_reboot("test/device/x.cpp:4:t:PASS")
    assert not host.is_reboot("CPA_REC_BUDGET rebooting=0")


class LineSerial:
    """A port that plays back device lines, then stays silent."""

    def __init__(self, lines: list[bytes]) -> None:
        self.data = b"".join(line + b"\r\n" for line in lines)
        self.port = self.baudrate = self.timeout = None
        self.dsrdtr = self.rts = self.dtr = self.closed = False

    def open(self) -> None:
        pass

    def read(self, n: int) -> bytes:
        chunk, self.data = self.data[:n], self.data[n:]
        return chunk

    def write(self, data: bytes) -> None:
        pass

    def close(self) -> None:
        self.closed = True


def test_run_stops_at_a_crash_instead_of_rerunning_the_suite(capsys) -> None:
    # docs/07 §6.7 #1: after a panic the suite reboots and runs again, forever.
    s = LineSerial(
        [
            b"test/device/x.cpp:1:test_a:PASS",
            b"Guru Meditation Error: Core  1 panic'ed (Unhandled debug exception).",
            b"Backtrace: 0x42001234:0x3fcebf00",
            b"Rebooting...",
            b"test/device/x.cpp:1:test_a:PASS",
        ]
    )
    assert host.run("/dev/cu.test", factory=lambda: s) == 1
    out = capsys.readouterr().out
    assert s.closed and "Backtrace: 0x42001234" in out and "rebooted" in out
    assert out.count("test_a:PASS") == 1


def test_run_returns_0_after_the_summary() -> None:
    s = LineSerial([b"test/device/x.cpp:1:test_a:PASS", b"1 Tests 0 Failures 0 Ignored"])
    assert host.run("/dev/cu.test", factory=lambda: s) == 0 and s.closed


def test_find_port_prefers_the_environment(monkeypatch) -> None:
    monkeypatch.setenv("CPA_TEST_PORT", "/dev/cu.test")
    assert host.find_port(glob=lambda _: []) == "/dev/cu.test"
    monkeypatch.delenv("CPA_TEST_PORT")
    assert (
        host.find_port(glob=lambda _: ["/dev/cu.usbmodem2", "/dev/cu.usbmodem1"])
        == "/dev/cu.usbmodem1"
    )
    assert host.find_port(glob=lambda _: []) is None


# ---- stack_budget -------------------------------------------------------------------


def su(tmp: Path, rel: str, text: str) -> None:
    p = tmp / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


def test_stack_budget_loads_app_frames(tmp_path: Path) -> None:
    su(tmp_path, "src/main.cpp.su", "main.cpp:40:6:void loop()\t48\tstatic\n")
    su(
        tmp_path,
        "lib1/shell/runtime.cpp.su",
        "runtime.cpp:1:1:void cpa::Runtime::loop(uint32_t, cpa::KeySet)\t96\tstatic\n",
    )
    su(
        tmp_path,
        "lib2/board/board_module.cpp.su",
        "b.cpp:1:1:cpa::BoardModule::render() const\t64\tstatic\n",
    )
    su(tmp_path, "lib3/M5GFX/x.cpp.su", "x.cpp:1:1:m5::f()\t9000\tstatic\n")
    names = sorted(f.name for f in stack_budget.load_app_frames(tmp_path))
    assert names == [
        "cpa::BoardModule::render() const",
        "void cpa::Runtime::loop(uint32_t, cpa::KeySet)",
        "void loop()",
    ]


def test_stack_budget_chains_are_within_the_loop_stack() -> None:
    assert stack_budget.LOOP_BUDGET == 12 * 1024
    for title, (budget_b, steps) in stack_budget.CHAINS.items():
        assert budget_b <= stack_budget.LOOP_BUDGET, title
        assert steps, title


def test_stack_budget_main(tmp_path: Path) -> None:
    su(tmp_path, "src/main.cpp.su", "main.cpp:40:6:void loop()\t48\tstatic\n")
    assert (
        stack_budget.main([str(tmp_path)], chains={"c": (100, [(r"\bloop\(", 1)])}) == 0
    )
    assert (
        stack_budget.main([str(tmp_path)], chains={"c": (10, [(r"\bloop\(", 1)])}) == 1
    )
    su(
        tmp_path,
        "src/lib.cpp.su",
        f"lib.cpp:1:1:void m5::big()\t{stack_budget.APP_FRAME_MAX + 1}\tdynamic\n",
    )
    assert (
        stack_budget.main([str(tmp_path)], chains={}) == 0
    )  # library code: only counted in chains
    su(
        tmp_path,
        "src/big.cpp.su",
        f"big.cpp:1:1:void cpa::big()\t{stack_budget.APP_FRAME_MAX + 1}\tstatic\n",
    )
    assert stack_budget.main([str(tmp_path)], chains={}) == 1
    assert stack_budget.main([str(tmp_path / "none")], chains={}) == 1


def test_stack_budget_device_suite_frames_share_the_app_frame_cap(tmp_path: Path) -> None:
    # docs/07 §6.7 #1: a 6,256 B frame in test_rec_sd overflowed the 8 KB loop stack.
    su(
        tmp_path,
        "test/device/test_a/test_main.cpp.su",
        "test_main.cpp:124:13:void test_cut_off()\t6256\tstatic\n",
    )
    cap = stack_budget.APP_FRAME_MAX
    su(
        tmp_path,
        "test/device/test_b/test_main.cpp.su",
        f"rec_harness.h:23:12:int rectest::remove_tree(const string&, int)\t{cap}\tstatic\n",
    )
    assert stack_budget.check_test_frames(tmp_path, "device/test_a") == 1
    assert stack_budget.check_test_frames(tmp_path, "device/test_b") == 0  # test_a's are not its own
    su(tmp_path, "test/device/test_b/more.cpp.su", f"more.cpp:1:1:void f()\t{cap + 1}\tstatic\n")
    assert stack_budget.check_test_frames(tmp_path, "device/test_b") == 1
    assert stack_budget.check_test_frames(tmp_path, "device/none") == 1  # nothing to check


def test_stack_budget_loads_the_new_hal_frames(tmp_path: Path) -> None:
    for name in ("sd_card", "part_facts"):
        su(
            tmp_path,
            f"lib4/hal/{name}.cpp.su",
            f"{name}.cpp:1:1:cpa::FatErr cpa::VfsFatPort::write(const char*, size_t)\t32\tstatic\n",
        )
    frames = stack_budget.load_app_frames(tmp_path)
    assert len(frames) == 2


def _sd_chains() -> dict:
    return {
        title: steps
        for title, (_, steps) in stack_budget.CHAINS.items()
        if any("SdStore" in pattern for pattern, _ in steps)
    }


def _has(steps, *needles: str) -> bool:
    return all(any(n in pattern for pattern, _ in steps) for n in needles)


def test_stack_budget_sd_chains_carry_the_fatfs_allowance() -> None:
    assert stack_budget.FATFS == 2048
    sd = _sd_chains()
    assert len(sd) >= 2
    for title, steps in sd.items():
        assert _has(steps, "__fatfs_allowance__", "__framework_allowance__"), title
        assert _has(steps, "VfsFatPort"), title
    for title, (_, steps) in stack_budget.CHAINS.items():
        if title not in sd:
            assert not _has(steps, "__fatfs_allowance__"), title


def test_stack_budget_has_the_two_new_sd_chains() -> None:
    sd = list(_sd_chains().values())
    assert any(
        _has(steps, "deliver_reply", "apply_outbox", "outbox_apply", "move_file", "SdStore")
        for steps in sd
    ), "ack -> apply_outbox -> outbox_apply -> move_file -> SdStore"
    assert any(
        _has(steps, "setup", "Runtime::begin", "outbox_migrate", "commit_file", "SdStore")
        for steps in sd
    ), "setup -> Runtime::begin -> outbox_migrate -> commit_file -> SdStore"


def test_stack_budget_reads_the_v040_libraries(tmp_path: Path) -> None:
    # docs/13 §10.1: the Tab menu (lib/help) and the home launcher (modules/home).
    su(
        tmp_path,
        "lib5/help/menu_model.cpp.su",
        "menu_model.cpp:1:1:cpa::MenuContent cpa::menu_content(cpa::MenuPage)\t240\tstatic\n",
    )
    su(
        tmp_path,
        "lib6/home/home_module.cpp.su",
        "home_module.cpp:1:1:void cpa::HomeModule::render(cpa::Canvas&) const\t96\tstatic\n",
    )
    names = sorted(f.name for f in stack_budget.load_app_frames(tmp_path))
    assert names == [
        "cpa::MenuContent cpa::menu_content(cpa::MenuPage)",
        "void cpa::HomeModule::render(cpa::Canvas&) const",
    ]


@pytest.mark.parametrize(
    "needles",
    [
        ("Shell::menu_key_event", "menu_step", "menu_content"),  # a key in the Tab menu
        ("Shell::render_menu", "menu_draw", "menu_content"),  # the menu's rows built
        ("Shell::render_menu", "menu_draw", "menu_render", "M5Display"),  # and drawn
        ("Shell::render_content", "HomeModule::render", "M5Display"),  # the launcher
        ("Shell::render", "render_lock_screen", "M5Display"),  # the lock card / ● line
        ("Runtime::key_event", "Shell::lock_gate", "lock_key"),  # a key while locked
    ],
)
def test_stack_budget_has_the_v040_chains(needles) -> None:
    assert any(_has(steps, *needles) for _, steps in stack_budget.CHAINS.values()), needles


def test_stack_budget_counts_the_fatfs_allowance(tmp_path: Path) -> None:
    su(tmp_path, "src/main.cpp.su", "main.cpp:40:6:void loop()\t48\tstatic\n")
    steps = [(r"\bloop\(", 1), stack_budget.FATFS_STEP, (r"?__framework_allowance__", 1)]
    total = 48 + stack_budget.FATFS + stack_budget.FRAMEWORK
    assert stack_budget.main([str(tmp_path)], chains={"c": (total, steps)}) == 0
    assert stack_budget.main([str(tmp_path)], chains={"c": (total - 1, steps)}) == 1


# ---- check_partitions ---------------------------------------------------------------

DATA, APP = 0x01, 0x00


def entry(label: str, ptype: int, subtype: int, offset: int, size: int) -> bytes:
    return parts.encode_entry(label, ptype, subtype, offset, size)


def table(*entries: bytes) -> bytes:
    body = b"".join(entries)
    return body + b"\xff" * (0xC00 - len(body))


GOOD = (
    entry("nvs", DATA, 0x02, 0x9000, 0x5000),
    entry("otadata", DATA, 0x00, 0xE000, 0x2000),
    entry("app0", APP, 0x10, 0x10000, 0x330000),
    entry("app1", APP, 0x11, 0x340000, 0x330000),
    entry("cpafs", DATA, 0x82, 0x670000, 0x180000),
    entry("coredump", DATA, 0x03, 0x7F0000, 0x10000),
)


def test_partitions_decode_round_trip() -> None:
    rows = parts.decode(table(*GOOD))
    assert [r.label for r in rows] == ["nvs", "otadata", "app0", "app1", "cpafs", "coredump"]
    cpafs = rows[4]
    assert (cpafs.type, cpafs.subtype, cpafs.offset, cpafs.size) == (DATA, 0x82, 0x670000, 0x180000)


def test_partitions_good_table_passes(tmp_path: Path) -> None:
    f = tmp_path / "partitions.bin"
    f.write_bytes(table(*GOOD))
    assert parts.problems(parts.decode(f.read_bytes())) == []
    assert parts.main([str(f)]) == 0


def test_partitions_md5_entry_is_skipped() -> None:
    md5 = b"\xeb\xeb" + b"\xff" * 14 + b"\x00" * 16
    assert len(parts.decode(table(*GOOD, md5))) == 6


def test_partitions_spiffs_label_fails() -> None:
    rows = parts.decode(table(*GOOD[:4], entry("spiffs", DATA, 0x82, 0x670000, 0x180000), GOOD[5]))
    found = parts.problems(rows)
    assert any("spiffs" in p for p in found)
    assert any("cpafs" in p for p in found)  # and no cpafs


def test_partitions_test_app_fails() -> None:
    rows = parts.decode(table(*GOOD, entry("test", APP, 0x20, 0x7F0000, 0x10000)))
    assert any("test" in p for p in parts.problems(rows))


def test_partitions_cpafs_must_keep_offset_and_size() -> None:
    moved = entry("cpafs", DATA, 0x82, 0x680000, 0x180000)
    small = entry("cpafs", DATA, 0x82, 0x670000, 0x70000)
    assert parts.problems(parts.decode(table(*GOOD[:4], moved, GOOD[5])))
    assert parts.problems(parts.decode(table(*GOOD[:4], small, GOOD[5])))


def test_partitions_bad_input_fails(tmp_path: Path) -> None:
    assert parts.main([str(tmp_path / "missing.bin")]) == 1
    junk = tmp_path / "junk.bin"
    junk.write_bytes(b"\x00" * 64)
    assert parts.main([str(junk)]) == 1
    with pytest.raises(ValueError):
        parts.encode_entry("x" * 17, DATA, 0x82, 0, 0)


# ---- make_image ---------------------------------------------------------------------


def fake_build(tmp: Path) -> Path:
    build = tmp / ".pio" / "build" / "cardputer-adv"
    build.mkdir(parents=True)
    (build / "bootloader.bin").write_bytes(b"B" * 16)
    (build / "partitions.bin").write_bytes(table(*GOOD))
    (build / "firmware.bin").write_bytes(b"F" * 64)
    return build


class Merge:
    """Stands in for esptool merge_bin: records the command, writes the -o file."""

    def __init__(self, code: int = 0) -> None:
        self.code, self.argv = code, None

    def __call__(self, argv, **kwargs):
        self.argv = argv
        if self.code == 0:
            Path(argv[argv.index("-o") + 1]).write_bytes(b"M" * 128)
        return subprocess.CompletedProcess(argv, self.code, "", "")


def test_make_image_names() -> None:
    assert make_image.image_names("v0.2.0") == ("cpa-v0.2.0.bin", "cpa-v0.2.0-app.bin")
    assert make_image.image_names("v0.2.0-dev") == ("cpa-v0.2.0-dev.bin", "cpa-v0.2.0-dev-app.bin")


def test_make_image_tag_rules() -> None:
    assert make_image.check_tag("v0.2.0") is None
    assert make_image.check_tag("v10.20.30-rc.1") is None
    assert make_image.check_tag("0.2.0")  # needs the v
    assert make_image.check_tag("v0.2")
    assert make_image.check_tag("v0.2.0/../x")
    assert make_image.check_tag("v0.2.0-much-too-long")  # Launcher shows <= 20 characters


def test_make_image_merge_command(tmp_path: Path) -> None:
    build = fake_build(tmp_path)
    cmd = make_image.merge_command("pio", build, tmp_path / "boot_app0.bin", tmp_path / "out.bin")
    assert cmd[:7] == ["pio", "pkg", "exec", "--package", "tool-esptoolpy", "--", "esptool.py"]
    assert cmd[cmd.index("-o") + 1] == str(tmp_path / "out.bin")
    tail = cmd[cmd.index("0x0") :]
    assert tail == [
        "0x0", str(build / "bootloader.bin"),
        "0x8000", str(build / "partitions.bin"),
        "0xe000", str(tmp_path / "boot_app0.bin"),
        "0x10000", str(build / "firmware.bin"),
    ]  # fmt: skip
    for flag in ("--flash_mode", "--flash_freq", "--flash_size"):
        assert cmd[cmd.index(flag) + 1] == "keep"


def test_make_image_writes_both_images_and_sums(tmp_path: Path) -> None:
    fake_build(tmp_path)
    boot = tmp_path / "boot_app0.bin"
    boot.write_bytes(b"\x00" * 32)
    out = tmp_path / "dist"
    merge = Merge()
    argv = ["--env", "cardputer-adv", "--tag", "v0.2.0-dev", "--out", str(out)]
    rc = make_image.main([*argv, "--project", str(tmp_path), "--boot-app0", str(boot)], run=merge)
    assert rc == 0
    assert (out / "cpa-v0.2.0-dev.bin").read_bytes() == b"M" * 128
    assert (out / "cpa-v0.2.0-dev-app.bin").read_bytes() == b"F" * 64
    sums = (out / "SHA256SUMS").read_text().splitlines()
    assert [line.split("  ")[1] for line in sums] == ["cpa-v0.2.0-dev-app.bin", "cpa-v0.2.0-dev.bin"]
    assert all(len(line.split("  ")[0]) == 64 for line in sums)


def test_make_image_refuses(tmp_path: Path) -> None:
    boot = tmp_path / "boot_app0.bin"
    boot.write_bytes(b"\x00")
    common = ["--env", "cardputer-adv", "--out", str(tmp_path / "d"), "--project", str(tmp_path)]
    common += ["--boot-app0", str(boot)]
    assert make_image.main([*common, "--tag", "v0.2.0"], run=Merge()) == 1  # not built yet
    build = fake_build(tmp_path)
    assert make_image.main([*common, "--tag", "bad"], run=Merge()) == 1
    assert make_image.main([*common, "--tag", "v0.2.0"], run=Merge(code=2)) == 1  # esptool failed
    (build / "partitions.bin").write_bytes(table(*GOOD[:4], entry("spiffs", DATA, 0x82, 0x670000, 0x180000)))
    assert make_image.main([*common, "--tag", "v0.2.0"], run=Merge()) == 1  # a table the Launcher must not get
