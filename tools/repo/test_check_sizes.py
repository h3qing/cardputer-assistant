"""Tests for tools/repo/check_sizes.py (written before the implementation)."""

from __future__ import annotations

from pathlib import Path

import check_sizes
import pytest


def _write(path: Path, lines: int) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("x = 1\n" * lines, encoding="utf-8")
    return path


# --- count_lines -------------------------------------------------------------


def test_count_lines_empty_file_is_zero(tmp_path: Path) -> None:
    f = tmp_path / "a.py"
    f.write_bytes(b"")
    assert check_sizes.count_lines(f) == 0


def test_count_lines_counts_trailing_line_without_newline(tmp_path: Path) -> None:
    f = tmp_path / "a.py"
    f.write_bytes(b"a\nb")
    assert check_sizes.count_lines(f) == 2


def test_count_lines_with_final_newline(tmp_path: Path) -> None:
    f = tmp_path / "a.py"
    f.write_bytes(b"a\nb\n")
    assert check_sizes.count_lines(f) == 2


# --- classify ----------------------------------------------------------------


@pytest.mark.parametrize(
    ("lines", "expected"),
    [(0, "ok"), (400, "ok"), (401, "warn"), (800, "warn"), (801, "fail")],
)
def test_classify_thresholds(lines: int, expected: str) -> None:
    assert check_sizes.classify(lines, warn=400, fail=800) == expected


# --- is_ignored / iter_files ---------------------------------------------------


@pytest.mark.parametrize(
    "rel",
    [
        ".pio/build/x.cpp",
        "firmware/app/.pio/libdeps/a.h",
        ".venv/lib/site.py",
        "daemon/.venv/x.py",
        "docs/research/tool.py",
        ".git/hooks/pre-commit.sh",
    ],
)
def test_is_ignored_true(rel: str) -> None:
    assert check_sizes.is_ignored(Path(rel))


@pytest.mark.parametrize("rel", ["tools/repo/a.py", "docs/adr/x.py", "research/a.py"])
def test_is_ignored_false(rel: str) -> None:
    assert not check_sizes.is_ignored(Path(rel))


def test_iter_files_skips_ignored_dirs_and_non_code(tmp_path: Path) -> None:
    keep = _write(tmp_path / "src" / "main.cpp", 1)
    _write(tmp_path / "src" / ".pio" / "gen.cpp", 1)
    _write(tmp_path / ".venv" / "lib.py", 1)
    _write(tmp_path / "docs" / "research" / "x.py", 1)
    _write(tmp_path / "data" / "vectors.json", 1)
    found = tuple(check_sizes.iter_files((tmp_path,)))
    assert found == (keep,)


def test_iter_files_accepts_single_file(tmp_path: Path) -> None:
    f = _write(tmp_path / "one.py", 3)
    assert tuple(check_sizes.iter_files((f,))) == (f,)


# --- main --------------------------------------------------------------------


def test_main_passes_with_only_warnings(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    _write(tmp_path / "big.py", 450)
    _write(tmp_path / "small.py", 10)
    code = check_sizes.main(["--warn", "400", "--fail", "800", str(tmp_path)])
    out = capsys.readouterr().out
    assert code == 0
    assert "WARN" in out and "big.py" in out
    assert "small.py" not in out
    assert "2 files" in out


def test_main_fails_when_file_exceeds_fail(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    _write(tmp_path / "huge.cpp", 801)
    code = check_sizes.main(["--warn", "400", "--fail", "800", str(tmp_path)])
    out = capsys.readouterr().out
    assert code == 1
    assert "FAIL" in out and "huge.cpp" in out


def test_main_defaults_to_400_and_800(tmp_path: Path) -> None:
    _write(tmp_path / "a.py", 801)
    assert check_sizes.main([str(tmp_path)]) == 1


def test_main_rejects_missing_path(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    code = check_sizes.main([str(tmp_path / "nope")])
    assert code == 2
    assert "not found" in capsys.readouterr().err


def test_main_rejects_warn_not_below_fail(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    code = check_sizes.main(["--warn", "800", "--fail", "800", str(tmp_path)])
    assert code == 2
    assert "--warn" in capsys.readouterr().err


def test_main_rejects_non_positive_threshold(tmp_path: Path) -> None:
    with pytest.raises(SystemExit):
        check_sizes.main(["--warn", "0", str(tmp_path)])
