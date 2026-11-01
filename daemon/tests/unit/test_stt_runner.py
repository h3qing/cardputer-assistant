"""The command runner (docs/22 §4.3): no shell, a sandbox around every run, argv placeholders, a whitelisted
environment, an empty 0700 work directory, stdin closed, stdout capped at 256 KiB, stderr dropped, timeouts
kill the whole process group. Uses a stand-in for sandbox-exec, so it runs on Linux CI too."""

from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

from cpa_stt import runner
from tests.support import stt_tree

MACH = ("com.apple.MTLCompilerService",)


@pytest.fixture
def env(tmp_path):
    return runner.Env(sandbox_exec=str(stt_tree.fake_sandbox(tmp_path)), cache_dir="/private/var/folders/x/C",
                      mach=MACH, home=str(tmp_path / "home"))  # fmt: skip


@pytest.fixture
def wav(tmp_path):
    path = tmp_path / "in.wav"
    path.write_bytes(stt_tree.tone(1600))
    return path


def _run(env, wav, *flags, timeout=20.0, tick=lambda: True, offset=0, **kw):
    with runner.workdir() as wd:
        return runner.run(stt_tree.fake_argv(*flags), wav, offset, timeout, env, wd, tick=tick, **kw), wd


def test_fill_argv_replaces_whole_placeholders_only():
    argv = ("/bin/w", "-ot", "{offset_ms}", "-f", "{wav}", "--keep={wav}x")
    assert runner.fill_argv(argv, "/tmp/a.wav", 200) == ("/bin/w", "-ot", "200", "-f", "/tmp/a.wav", "--keep={wav}x")


def test_timeout_grows_with_the_recording():
    assert runner.timeout_for(120, 10) == 120
    assert runner.timeout_for(120, 60) == 150
    assert runner.timeout_for(10, 0) == 30


def test_a_run_returns_stdout_and_timing(env, wav):
    out, _ = _run(env, wav, "--say", "你好", "--describe", offset=200)
    assert out.ok and out.exit == 0 and not out.timed_out
    assert "你好" in out.stdout.decode() and "samples=1600 offset=200" in out.stdout.decode()
    assert out.took_ms >= 0


def test_the_command_runs_inside_the_sandbox_profile(env, wav):
    _, wd = _run(env, wav, "--say", "x")
    text = Path(env.sandbox_exec + ".profile").read_text()
    assert f'(deny file-write* (require-not (subpath "{os.path.realpath(wd)}")))' in text
    assert '(subpath "/private/var/folders/x/C/com.apple.metal")' in text
    assert '(global-name "com.apple.MTLCompilerService")' in text


def test_environment_is_whitelisted(env, wav, monkeypatch):
    monkeypatch.setenv("CPA_STT_SECRET", "hunter2")
    out, _ = _run(env, wav, "--env")
    line = next(x for x in out.stdout.decode().splitlines() if x.startswith("env "))
    keys = set(line.split()[1].split(","))
    assert "CPA_STT_SECRET" not in keys and {"PATH", "HOME", "LANG"} <= keys
    assert keys <= {"PATH", "HOME", "LANG", "PWD", "SHLVL", "_", "OLDPWD", "__CF_USER_TEXT_ENCODING"}
    assert "PATH=/usr/bin:/bin LANG=en_US.UTF-8" in line


def test_workdir_is_empty_private_and_removed(env, wav):
    out, wd = _run(env, wav, "--cwd", "--write-cwd")
    text = out.stdout.decode()
    assert "cwd entries=0 mode=0o700" in text and f"path={os.path.realpath(wd)}" in text
    assert not os.path.exists(wd)


def test_stdin_is_closed_and_stderr_dropped(env, wav):
    out, _ = _run(env, wav, "--stdin", "--stderr", "SECRET-TRANSCRIPT")
    assert "stdin=0" in out.stdout.decode() and b"SECRET-TRANSCRIPT" not in out.stdout


def test_exit_codes_are_reported(env, wav):
    out, _ = _run(env, wav, "--exit", "3")
    assert out.exit == 3 and not out.ok


def test_stdout_beyond_the_cap_is_a_failed_run_not_a_cut_transcript(env, wav):
    out, _ = _run(env, wav, "--flood")
    assert out.truncated and not out.ok and out.exit == 0 and len(out.stdout) == runner.STDOUT_MAX
    out, _ = _run(env, wav, "--flood", stdout_max=2 << 20)
    assert out.ok and not out.truncated and len(out.stdout) > 1 << 20


def test_the_stdout_cap_grows_with_the_recording():
    assert runner.stdout_cap(0) == 256 * 1024
    assert runner.stdout_cap(4 * 3600) == 256 * 1024 + 160 * 4 * 3600


def test_timeout_kills_the_command(env, wav):
    start = time.monotonic()
    out, _ = _run(env, wav, "--sleep", "30", timeout=1.0)
    assert out.timed_out and not out.ok and time.monotonic() - start < 10


def test_tick_runs_while_the_command_runs(env, wav):
    ticks = []
    out, _ = _run(env, wav, "--sleep", "0.6", tick=lambda: ticks.append(1) or True)
    assert len(ticks) >= 2 and out.ok


def test_a_tick_that_says_the_claim_is_gone_kills_the_command(env, wav):
    start = time.monotonic()
    out, _ = _run(env, wav, "--sleep", "30", tick=lambda: False)
    assert out.lost and not out.timed_out and not out.ok and time.monotonic() - start < 10


def test_no_sandbox_means_no_run(tmp_path, wav):
    gone = runner.Env(sandbox_exec=str(tmp_path / "missing"), cache_dir=None, mach=(), home=str(tmp_path))
    log = tmp_path / "calls.log"
    with pytest.raises(runner.SandboxMissing), runner.workdir() as wd:
        runner.run(stt_tree.fake_argv("--log", str(log)), wav, 0, 5, gone, wd)
    assert not log.exists()


def test_write_wav_concatenates_segments(tmp_path):
    a, b = stt_tree.tone(160), stt_tree.tone(320)
    (tmp_path / "seg-000.wav").write_bytes(a)
    (tmp_path / "seg-001.wav").write_bytes(b)
    out = tmp_path / "run.wav"
    runner.write_wav(out, [tmp_path / "seg-000.wav", tmp_path / "seg-001.wav"])
    assert out.read_bytes() == stt_tree.wav_header(960) + a[44:] + b[44:]


def test_cache_dir_comes_from_getconf():
    found = runner.darwin_cache_dir(lambda argv: "/var/folders/ab/cd/C/\n")
    assert found == os.path.realpath("/var/folders/ab/cd/C")
    assert runner.darwin_cache_dir(lambda argv: "") is None


def test_child_env_passes_only_the_corefoundation_encoding_through(monkeypatch):
    monkeypatch.setenv("__CF_USER_TEXT_ENCODING", "0x1F5:0x0:0x0")
    assert runner.child_env("/h")["__CF_USER_TEXT_ENCODING"] == "0x1F5:0x0:0x0"  # else CF reads the real home
    monkeypatch.delenv("__CF_USER_TEXT_ENCODING")
    assert runner.child_env("/h") == {"PATH": "/usr/bin:/bin", "HOME": "/h", "LANG": "en_US.UTF-8"}
