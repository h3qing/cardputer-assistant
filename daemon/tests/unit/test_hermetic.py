"""#71: every test runs on a fresh temp HOME with stub programs first on PATH, and an audit hook refuses the
real home and the real claude / whisper-cli / launchctl / mdfind (tests/support/hermetic.py)."""

from __future__ import annotations

import getpass
import os
import pwd
import subprocess
from pathlib import Path

import pytest

from tests.support import hermetic
from tests.support.fake_claude import FakeClaude

SOCK_TAIL = b"/Library/Application Support/CardputerAssistant/cpad.sock"


@pytest.mark.parametrize("run", [1, 2])
def test_each_test_gets_its_own_empty_short_temp_home(run):
    home = Path(os.environ["HOME"])
    assert Path.home() == home and Path("~").expanduser() == home and not any(home.iterdir())
    assert os.path.realpath(home) != os.path.realpath(hermetic.original_home())
    assert len(os.fsencode(os.path.realpath(home)) + SOCK_TAIL) <= 103  # the default cpad.sock fits AF_UNIX
    (home / "left-behind").write_text(str(run))


def test_cpa_and_claude_variables_are_unset():
    assert [name for name in hermetic.SCRUB if name in os.environ] == []


def test_path_environment_and_pwd_stay_out_of_the_real_home():
    homes, allowed = hermetic.guard().roots
    assert [folder for folder in os.environ["PATH"].split(os.pathsep) if hermetic.in_home(folder, homes, allowed)] == []
    named = [
        k for k, v in os.environ.items() if k.startswith(hermetic.SESSION_VARS) or hermetic.mentions(v, homes, allowed)
    ]
    assert named == []
    assert os.path.expanduser("~" + getpass.getuser()) == os.environ["HOME"] == pwd.getpwuid(os.getuid()).pw_dir


@pytest.mark.parametrize("name", hermetic.STUBS)
def test_programs_on_path_are_stubs_that_only_log_their_arguments(name):
    done = subprocess.run([name, "--version"], capture_output=True, text=True, check=False)
    assert done.returncode == 127 and "test stub" in done.stderr
    assert f"{name} --version" in hermetic.stub_calls()


def test_the_live_guard_refuses_the_real_home_but_not_the_test_home_or_the_repo():
    watch, real = hermetic.guard(), hermetic.original_home()
    claim = os.path.join(real, "CardputerNotes", "recordings", ".transcriber")
    assert watch.refused("open", (claim, "r", 0)) == f"open {claim}"
    assert watch.refused("socket.connect", (None, os.path.join(real, "cpad.sock")))
    assert watch.refused("os.mkdir", (os.path.join(real, ".claude"), 0o700, None))
    assert watch.refused("open", (str(Path.home() / ".claude.json"), "w", 0)) is None
    assert watch.refused("open", (str(hermetic.REPO / "README.md"), "r", 0)) is None
    assert watch.refused("socket.connect", (None, ("127.0.0.1", 80))) is None


def _guard(tmp_path: Path) -> tuple[hermetic.Guard, Path]:
    claude = tmp_path / "bin" / "claude"
    claude.parent.mkdir()
    claude.write_text("#!/bin/sh\n")
    claude.chmod(0o755)
    homes = hermetic.both_forms((str(tmp_path / "home"),))
    allowed = hermetic.both_forms((str(tmp_path / "home" / "repo"),))
    return hermetic.Guard(homes, allowed, {os.path.realpath(claude): "claude"}), claude


def test_guard_refuses_records_and_allows(tmp_path):
    watch, claude = _guard(tmp_path)
    with pytest.raises(hermetic.RealHomeAccess):
        watch("open", (str(tmp_path / "home" / ".claude.json"), "r", 0))
    watch("open", (str(tmp_path / "home" / "repo" / "x.py"), "r", 0))
    watch("open", (3, "r", 0))
    with pytest.raises(PermissionError):
        watch("subprocess.Popen", (None, ["claude", "--version"], None, {"PATH": str(claude.parent)}))
    with pytest.raises(PermissionError):
        watch("subprocess.Popen", ("/usr/bin/sandbox-exec", ["sandbox-exec", "-p", "x", str(claude)], None, None))
    watch("subprocess.Popen", (None, ["claude"], None, {"PATH": "/nonexistent", "HOME": str(tmp_path)}))
    with pytest.raises(PermissionError), pytest.MonkeyPatch.context() as mp:
        mp.setattr(os, "defpath", str(claude.parent))
        watch("subprocess.Popen", (None, ["claude"], None, {}))  # no PATH: exec searches os.defpath
    assert len(watch.violations) == 4 and "runs the real claude" in watch.violations[-1]


def test_guard_lets_a_permitted_program_run_and_stops_when_inactive(tmp_path):
    watch, claude = _guard(tmp_path)
    watch.permitted = frozenset({"claude"})
    watch("subprocess.Popen", (str(claude), [str(claude)], None, None))
    watch.active = False
    watch("open", (str(tmp_path / "home" / "x"), "r", 0))
    assert watch.violations == []


@pytest.mark.real_programs("whisper-cli")
def test_the_real_programs_marker_permits_only_what_it_names():
    assert hermetic.guard().permitted == frozenset({"whisper-cli"})


def test_real_program_comes_from_the_starting_path_not_the_stubs():
    found = hermetic.real_program("launchctl")
    assert found is None or not found.startswith(str(hermetic.stub_dir()))
    with pytest.raises(KeyError):
        hermetic.real_program("python3")


def test_spawns_that_name_a_home_or_drop_home_are_refused(tmp_path):
    watch, claude = _guard(tmp_path)
    home, ok = str(tmp_path / "home"), {"HOME": str(tmp_path)}
    cases = [
        ("subprocess.Popen", (None, ["/bin/cat", f"{home}/secret"], None, None)),
        ("subprocess.Popen", ("/bin/sh", ["/bin/sh", "-c", f"cat '{home}/my notes'"], None, None)),
        ("subprocess.Popen", (None, ["/bin/pwd"], home, None)),
        ("subprocess.Popen", (None, [f"{home}/.local/bin/cpa", "pause"], None, None)),
        ("subprocess.Popen", (None, ["/bin/sh"], None, {"PATH": "/bin", "HOME": home})),
        ("subprocess.Popen", (None, ["/bin/sh"], None, {"PATH": "/bin"})),
        ("subprocess.Popen", ("/bin/sh", ["/bin/sh", "-c", f"{claude} -p hi"], None, None)),
        ("os.system", (f"{claude} --version".encode(),)),
    ]
    for event, args in cases:
        with pytest.raises(hermetic.RealHomeAccess):
            watch(event, args)
    watch("subprocess.Popen", (None, ["/bin/cat", f"{home}/repo/x"], f"{home}/repo", ok))
    watch("os.system", (b"echo claude",))
    assert len(watch.violations) == len(cases)


def test_a_cpa_home_outside_the_home_is_guarded_too(tmp_path, monkeypatch):
    monkeypatch.setenv("CPA_HOME", str(tmp_path / "users-cpa"))
    watch = hermetic.new_guard(str(tmp_path / "home"), "")
    assert watch.refused("socket.connect", (None, str(tmp_path / "users-cpa" / "cpad.sock")))


def test_a_killed_runs_leftover_root_is_swept_at_the_next_start(tmp_path):
    gone = subprocess.Popen(["/bin/sh", "-c", "exit 0"])
    gone.wait()
    for name, owner in (("cpa-dead", gone.pid), ("cpa-live", os.getpid()), ("cpa-unowned", None)):
        (tmp_path / name).mkdir()
        if owner:
            (tmp_path / name / hermetic.OWNER).write_text(str(owner))
    (tmp_path / "cpa-file").write_text("not a root")
    hermetic.sweep_stale(tmp_path)
    assert sorted(p.name for p in tmp_path.iterdir()) == ["cpa-file", "cpa-live", "cpa-unowned"]
    assert (hermetic.session().root / hermetic.OWNER).read_text() == str(os.getpid())


def test_fake_claude_children_get_the_corefoundation_encoding(tmp_path, monkeypatch):
    monkeypatch.delenv("__CF_USER_TEXT_ENCODING", raising=False)
    assert FakeClaude(tmp_path / "fc").env["__CF_USER_TEXT_ENCODING"].startswith(f"0x{os.getuid():X}:")
