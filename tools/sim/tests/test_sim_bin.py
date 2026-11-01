"""Which cpa-sim the tests run: a missing build is skipped (or fails under CPA_SIM_REQUIRED=1); a build older
than its sources fails, so the tests never pass on a stale binary."""

import os
from pathlib import Path

from simtools.serve import SIM_SOURCES, sim_check, sim_problem, stale_against


def make_repo(root: Path) -> Path:
    for rel in SIM_SOURCES:
        path = root / rel
        if path.suffix:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x")
        else:
            (path / "core").mkdir(parents=True, exist_ok=True)
            (path / "core" / "a.cpp").write_text("x")
    binary = root / "firmware" / "sim" / ".pio" / "build" / "sim" / "cpa-sim"
    binary.parent.mkdir(parents=True)
    binary.write_text("bin")
    return binary


def age(path: Path, seconds: float) -> None:
    t = path.stat().st_mtime - seconds
    os.utime(path, (t, t))


def test_a_fresh_build_is_current(tmp_path):
    binary = make_repo(tmp_path)
    for p in tmp_path.rglob("*"):
        if p != binary and p.is_file():
            age(p, 60)
    assert stale_against(binary, tmp_path) is None


def test_a_newer_source_makes_it_stale(tmp_path):
    binary = make_repo(tmp_path)
    age(binary, 60)
    newer = stale_against(binary, tmp_path)
    assert newer is not None and newer.is_relative_to(tmp_path)


def test_build_output_hal_and_other_files_do_not_count(tmp_path):
    binary = make_repo(tmp_path)
    for p in tmp_path.rglob("*"):
        if p != binary and p.is_file():
            age(p, 60)
    ignored = ("firmware/sim/.pio/build/sim/x.o", "firmware/app/lib/hal/m5_display.cpp", "firmware/app/lib/core/a.md",
               "firmware/sim/out/v03/001.png", "firmware/app/test/core/test_x/test_main.cpp")  # fmt: skip
    for rel in ignored:
        (tmp_path / rel).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / rel).write_text("y")
    assert stale_against(binary, tmp_path) is None


def test_missing_build_is_a_skip_unless_required(tmp_path):
    problem = sim_problem(tmp_path, {})
    assert problem is not None and problem.missing and "pio run -d firmware/sim" in problem.message


def test_stale_build_is_not_missing(tmp_path):
    binary = make_repo(tmp_path)
    age(binary, 60)
    problem = sim_problem(tmp_path, {})
    assert problem is not None and not problem.missing and "older than" in problem.message


def test_an_explicit_binary_is_not_checked_against_this_tree(tmp_path):
    binary = make_repo(tmp_path)
    age(binary, 60)
    assert sim_problem(tmp_path, {"CPA_SIM_BIN": str(binary)}) is None
    problem = sim_problem(tmp_path, {"CPA_SIM_BIN": str(tmp_path / "nope")})
    assert problem is not None and problem.missing


def test_what_the_tests_do(tmp_path):
    assert sim_check(tmp_path, {})[0] == "skip"
    assert sim_check(tmp_path, {"CPA_SIM_REQUIRED": "1"})[0] == "fail"  # CI: a missing build fails
    binary = make_repo(tmp_path)
    age(binary, 60)
    assert sim_check(tmp_path, {}) == ("fail", sim_problem(tmp_path, {}).message)  # never a stale build
    os.utime(binary)
    assert sim_check(tmp_path, {"CPA_SIM_REQUIRED": "1"}) == ("run", "")
