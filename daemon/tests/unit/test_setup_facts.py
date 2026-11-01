"""cpa setup check reads this Mac (docs/27 §2.3) through SetupDeps only: a temp HOME, fake USB list, fake status,
fake fdesetup. Read-only: nothing under HOME changes."""

from __future__ import annotations

import json
import plistlib
from pathlib import Path

import pytest

from cpad.cli.main import CliDeps
from cpad.cli.main import main as cpa
from cpad.config.settings import Config as Cfg
from cpad.config.settings import Paths, replace_config, save_config
from cpad.device.port_finder import PortInfo
from cpad.service import launchd
from cpad.setup import facts as f
from cpad.setup.facts import SetupDeps

SERIAL = "02:00:00:00:00:01"
PY = "/x/tools/cpad/bin/python"


def fdesetup(state: str):
    return lambda argv, **_: launchd.Completed(0, f"FileVault is {state}.\n" if argv[-1] == "status" else "")


@pytest.fixture
def mac(tmp_path, monkeypatch):
    home = tmp_path / "home"
    monkeypatch.setenv("HOME", str(home))
    (home / "Library" / "LaunchAgents").mkdir(parents=True)
    paths = Paths.at(home / "cpa")
    paths.home.mkdir()
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    cpa_exe = bin_dir / "cpa"
    cpa_exe.write_text("#!/bin/sh\n")
    cpa_exe.chmod(0o755)
    deps = SetupDeps(home=home, agents_dir=home / "Library" / "LaunchAgents", runner=fdesetup("On"),
                     which=lambda name, path=None: None, ports=lambda: (), status=lambda _p: None,
                     settings=home / ".claude" / "settings.json", executable=PY, argv0=str(cpa_exe),
                     brew_bins=())  # fmt: skip
    return home, paths, deps


def config(paths: Paths, **changes) -> None:
    save_config(paths.config, replace_config(Cfg(), **changes))


def write_plist(deps: SetupDeps, python: str) -> None:
    launchd.plist_path(deps.agents_dir).write_bytes(plistlib.dumps({"ProgramArguments": [python, "-m", "cpad"]}))


def snapshot(root: Path) -> dict[str, float]:
    return {str(p): p.stat().st_mtime for p in sorted(root.rglob("*"))}


def test_a_fresh_mac(mac):
    home, paths, deps = mac
    got = f.gather(paths, deps)
    assert got.service == f.ServiceFacts(False, None, PY, False, None, f.__version__)
    assert got.notes == f.NotesFacts("~/CardputerNotes", False, True, False, None)
    assert got.label is None and got.device == f.DeviceFacts(None, ()) and got.filevault is True
    assert got.stt == f.SttFacts(False, (), None, brew=False)
    assert got.hooks.readable and got.hooks.ours == ()


def test_service_plist_python_and_live_version(mac):
    home, paths, deps = mac
    write_plist(deps, "/old/python")
    live = SetupDeps(**{**deps.__dict__, "status": lambda _p: {"version": "0.4.1"}})
    got = f.gather(paths, live).service
    assert (got.plist, got.python, got.running, got.version) == (True, "/old/python", True, "0.4.1")


def test_notes_under_documents_and_unwritable(mac, tmp_path):
    home, paths, deps = mac
    (home / "Documents" / "N").mkdir(parents=True)
    config(paths, notes_dir=str(home / "Documents" / "N"))
    assert f.gather(paths, deps).notes.tcc == "Documents"
    locked = tmp_path / "locked"
    locked.mkdir(mode=0o500)
    config(paths, notes_dir=str(locked / "notes"))
    try:
        assert f.gather(paths, deps).notes.writable is False
    finally:
        locked.chmod(0o700)


def test_notes_in_icloud(mac):
    home, paths, deps = mac
    cloud = home / "Library" / "Mobile Documents" / "com~apple~CloudDocs" / "Notes"
    cloud.mkdir(parents=True)
    config(paths, notes_dir=str(cloud))
    assert f.gather(paths, deps).notes.cloud is True


def test_device_codes_from_the_usb_list_only(mac):
    home, paths, deps = mac
    config(paths, device_serial=SERIAL)
    boards = (PortInfo("/dev/cu.b", "AA:BB:CC:DD:EE:FF", 0x303A, 0x1001), PortInfo("/dev/cu.a", SERIAL, 0x303A, 0x1001),
              PortInfo("/dev/cu.mouse", "X", 0x1234, 0x1))  # fmt: skip
    got = f.gather(paths, SetupDeps(**{**deps.__dict__, "ports": lambda: boards})).device
    assert got == f.DeviceFacts(f.device_code(SERIAL), (f.device_code(SERIAL), f.device_code("AABBCCDDEEFF")))


@pytest.mark.parametrize(("out", "want"), [("On", True), ("Off", False), ("Weird", None)])
def test_filevault(mac, out, want):
    home, paths, deps = mac
    assert f.gather(paths, SetupDeps(**{**deps.__dict__, "runner": fdesetup(out)})).filevault is want


def test_filevault_unknown_when_fdesetup_fails(mac):
    home, paths, deps = mac

    def boom(argv, **_):
        raise OSError("no")

    assert f.filevault(SetupDeps(**{**deps.__dict__, "runner": boom})) is None


def test_stt_models_and_a_link_into_downloads(mac):
    home, paths, deps = mac
    (home / "Downloads").mkdir()
    model = home / "Downloads" / "ggml-base.bin"
    model.write_bytes(b"lmgg")
    (paths.home / "models").mkdir()
    (paths.home / "models" / "ggml-base.bin").symlink_to(model)
    (paths.home / "models" / "ggml-gone.bin").symlink_to(home / "nope.bin")
    live = SetupDeps(**{**deps.__dict__, "which": lambda name, path=None: "/opt/homebrew/bin/whisper-cli"})
    assert f.gather(paths, live).stt == f.SttFacts(True, ("ggml-base.bin",), "Downloads")


def test_hooks_found_in_settings(mac):
    home, paths, deps = mac
    deps.settings.parent.mkdir()
    entry = {"type": "command", "command": "/old/cpa", "args": ["hook"]}
    other = {"type": "command", "command": "/bin/echo", "args": []}
    deps.settings.write_text(json.dumps({"hooks": {"Stop": [{"hooks": [entry, other]}], "X": "odd"}}))
    got = f.gather(paths, deps).hooks
    assert got == f.HooksFacts(True, ("/old/cpa",), deps.argv0)


def test_hooks_unreadable(mac):
    home, paths, deps = mac
    deps.settings.parent.mkdir()
    deps.settings.write_text("{nope")
    assert f.gather(paths, deps).hooks.readable is False


def test_check_reads_and_never_writes(mac):
    home, paths, deps = mac
    before = snapshot(home)
    f.gather(paths, deps)
    assert snapshot(home) == before


def test_cpa_setup_check_json_quiet_and_table(mac, capsys, monkeypatch):
    home, paths, deps = mac
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    cli = CliDeps(setup=deps)
    assert cpa(["setup", "check", "--json"], cli) == 0
    lines = capsys.readouterr().out.splitlines()
    ids = ["service", "notes", "name", "device", "disk", "stt", "hooks", "claude"]
    assert [json.loads(x)["id"] for x in lines] == ids
    assert cpa(["setup", "check", "--quiet"], cli) == 1 and capsys.readouterr().out == ""
    assert cpa(["setup", "check"], cli) == 0 and "required left" in capsys.readouterr().out
    assert cpa(["setup", "check", "--todo"], cli) == 0 and "To do" in capsys.readouterr().out
    monkeypatch.setenv("LANG", "zh_CN.UTF-8")
    assert cpa(["setup", "check"], cli) == 0 and "还差" in capsys.readouterr().out
    assert cpa(["setup", "check", "--todo"], cli) == 0 and "还要做" in capsys.readouterr().out


def test_quiet_is_zero_when_the_required_rows_are_ok(mac, monkeypatch):
    home, paths, deps = mac
    monkeypatch.setenv("CPA_HOME", str(paths.home))
    config(paths, device_serial=SERIAL)
    write_plist(deps, PY)
    ready = SetupDeps(**{**deps.__dict__, "status": lambda _p: {"version": f.__version__},
                         "ports": lambda: (PortInfo("/dev/cu.a", SERIAL, 0x303A, 0x1001),)})  # fmt: skip
    assert cpa(["setup", "check", "--quiet"], CliDeps(setup=ready)) == 0


def test_claude_found_on_path_or_in_its_own_folder_and_configured(mac):
    home, paths, deps = mac
    assert f.gather(paths, deps).claude == f.ClaudeFacts(None, None)
    own = home / ".local" / "bin" / "claude"
    own.parent.mkdir(parents=True)
    own.write_text("#!/bin/sh\n")
    own.chmod(0o755)
    assert f.gather(paths, deps).claude == f.ClaudeFacts(None, str(own))
    config(paths, claude_bin=str(own))
    assert f.gather(paths, deps).claude == f.ClaudeFacts(str(own), str(own))
    config(paths, claude_bin=str(home / "gone" / "claude"))
    assert f.gather(paths, deps).claude == f.ClaudeFacts(None, str(own))


def test_brew_found_by_which_or_in_its_folders(mac, tmp_path):
    home, paths, deps = mac
    assert f.gather(paths, deps).stt.brew is False
    which = SetupDeps(**{**deps.__dict__, "which": lambda name, path=None: "/b/brew" if name == "brew" else None})
    assert f.gather(paths, which).stt.brew is True


def test_json_and_quiet_do_not_mix(capsys):
    with pytest.raises(SystemExit):
        cpa(["setup", "check", "--json", "--quiet"])
