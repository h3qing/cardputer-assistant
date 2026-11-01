"""Strings that must not build (docs/21 §9.1): every i18n/*.cpp but ok.cpp breaks one Txt rule and has to
fail to compile, naming the rule's txt_* function; ok.cpp has to compile. The first line of each case says
which: `// expect: txt_too_wide` or `// expect: ok`.

Run: uv run --no-project --with pytest pytest firmware/app/test/compile_fail -q (CXX picks the compiler, default c++).
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
APP = HERE.parent.parent
INCLUDES = [APP / "lib" / "i18n", APP / "lib" / "core"]
CASES = sorted((HERE / "i18n").glob("*.cpp"))
CXX = os.environ.get("CXX", "c++")


def expectation(case: Path) -> str:
    first = case.read_text(encoding="utf-8").splitlines()[0]
    prefix = "// expect: "
    assert first.startswith(prefix), f"{case.name}: first line must be '{prefix}...'"
    return first[len(prefix) :].strip()


def compile_case(case: Path) -> subprocess.CompletedProcess[str]:
    flags = ["-std=c++17", "-fsyntax-only", "-Wall", "-Wextra", "-Werror"]
    incs = [f"-I{p}" for p in INCLUDES]
    return subprocess.run([CXX, *flags, *incs, str(case)], capture_output=True, text=True, check=False)


def test_there_are_cases_for_every_rule() -> None:
    wanted = {"ok", "txt_empty", "txt_control_char", "txt_han_in_english", "txt_missing_glyph"}
    wanted |= {"txt_format_mismatch", "txt_too_wide"}
    assert {expectation(c) for c in CASES} == wanted


@pytest.mark.skipif(shutil.which(CXX) is None, reason=f"no C++ compiler ({CXX})")
@pytest.mark.parametrize("case", CASES, ids=[c.stem for c in CASES])
def test_case(case: Path) -> None:
    want = expectation(case)
    done = compile_case(case)
    if want == "ok":
        assert done.returncode == 0, done.stderr
        return
    assert done.returncode != 0, f"{case.name} compiled, but it breaks {want}"
    assert want in done.stderr, done.stderr
