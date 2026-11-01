"""`cpa export` end to end (docs/15 §2.4, §8): folder and zip, shasum verifies, refusals exit 2, nothing written to
$CPA_HOME or the archive, no daemon and no device needed."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

from cpad.cli.main import main as cpa
from cpad.config.files import write_json
from tests.support import archive_tree as at


def _snapshot(base: Path) -> dict[str, tuple[int, int]]:
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns) for p in base.rglob("*") if p.is_file()}


@pytest.fixture
def env(tmp_path, monkeypatch):
    home = tmp_path / "home"
    home.mkdir(mode=0o700)
    notes = at.build(tmp_path / "notes")
    write_json(home / "config.json", {"notes_dir": str(notes)})
    monkeypatch.setenv("CPA_HOME", str(home))
    return home, notes


def test_export_folder_verifies_with_shasum(env, tmp_path, capsys):
    home, notes = env
    before = (_snapshot(home), _snapshot(notes))
    assert cpa(["export", str(tmp_path / "backup")]) == 0
    out = capsys.readouterr().out
    assert "2 daily notes" in out and "6 recordings" in out and "SHA256SUMS" in out
    assert "客户同步" not in out and "报价单" not in out
    if shutil.which("shasum"):
        check = subprocess.run(["shasum", "-a", "256", "-c", "SHA256SUMS"], cwd=tmp_path / "backup",
                               capture_output=True, text=True, check=False)  # fmt: skip
        assert check.returncode == 0, check.stdout + check.stderr
    assert (_snapshot(home), _snapshot(notes)) == before


def test_export_zip_with_range_and_no_audio(env, tmp_path, capsys):
    assert cpa(["export", "--zip", "--since", "2026-10-21", "--until", "2026-10-21", "--no-audio",
                str(tmp_path / "b.zip")]) == 0  # fmt: skip
    with zipfile.ZipFile(tmp_path / "b.zip") as zf:
        names = zf.namelist()
    assert "b/notes/2026-10-21.md" in names and "b/notes/2026-10-20.md" not in names
    assert not any(name.endswith(".wav") for name in names)


@pytest.mark.parametrize(
    "args",
    [["--since", "2026-02-30"], ["--since", "2026-10-22", "--until", "2026-10-21"], ["--zip"]],
)
def test_refusals_exit_2_without_writing(env, tmp_path, capsys, args):
    target = tmp_path / "backup"
    if args == ["--zip"]:
        target.mkdir()
    assert cpa(["export", *args, str(target)]) == 2
    assert capsys.readouterr().err.startswith("error: ")
    assert not target.exists() or not any(target.iterdir())


def test_refuses_a_target_inside_the_archive(env, capsys):
    _, notes = env
    assert cpa(["export", str(notes / "backup")]) == 2
    assert "outside the archive" in capsys.readouterr().err
    assert not (notes / "backup").exists()


def test_python_m_cpad_export_runs_without_a_daemon(env, tmp_path):
    home, _ = env
    run = subprocess.run([sys.executable, "-m", "cpad", "export", str(tmp_path / "out")], capture_output=True,
                         text=True, env={**os.environ, "CPA_HOME": str(home)}, check=False, timeout=60)  # fmt: skip
    assert run.returncode == 0, run.stderr
    assert (tmp_path / "out" / "SHA256SUMS").is_file()
