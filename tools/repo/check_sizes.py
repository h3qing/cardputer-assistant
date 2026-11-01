#!/usr/bin/env python3
"""Fail the build when a source file grows past the size budget.

Usage: check_sizes.py [--warn 400] [--fail 800] PATH [PATH ...]

Counts lines of source files (by suffix) under each PATH. Files above
--warn print a WARN line; files above --fail print a FAIL line and make
the exit code 1. Build output and research dumps are ignored.
Exit codes: 0 ok (warnings allowed), 1 a file exceeded --fail, 2 usage error.
Runs on the stock macOS python3 (3.9+); standard library only.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Iterable, Iterator, Sequence
from pathlib import Path

CODE_SUFFIXES = frozenset({".py", ".c", ".cc", ".cpp", ".h", ".hpp", ".ino", ".js", ".ts", ".sh"})
IGNORED_DIRS = frozenset({".pio", ".venv", ".git"})
IGNORED_PAIRS = frozenset({("docs", "research")})


def count_lines(path: Path) -> int:
    data = path.read_bytes()
    if not data:
        return 0
    return data.count(b"\n") + (0 if data.endswith(b"\n") else 1)


def classify(lines: int, warn: int, fail: int) -> str:
    if lines > fail:
        return "fail"
    if lines > warn:
        return "warn"
    return "ok"


def is_ignored(path: Path) -> bool:
    parts = path.parts
    if any(part in IGNORED_DIRS for part in parts):
        return True
    return any(parts[i : i + 2] in IGNORED_PAIRS for i in range(len(parts) - 1))


def _walk(root: Path, rel: Path) -> Iterator[Path]:
    for child in sorted((root / rel).iterdir()):
        child_rel = rel / child.name
        if is_ignored(child_rel):
            continue
        if child.is_dir() and not child.is_symlink():
            yield from _walk(root, child_rel)
        elif child.is_file() and child.suffix in CODE_SUFFIXES:
            yield child


def iter_files(paths: Iterable[Path]) -> Iterator[Path]:
    for root in paths:
        if root.is_file():
            if root.suffix in CODE_SUFFIXES and not is_ignored(root):
                yield root
        else:
            yield from _walk(root, Path())


def _positive_int(text: str) -> int:
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return value


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--warn", type=_positive_int, default=400)
    parser.add_argument("--fail", type=_positive_int, default=800)
    parser.add_argument("paths", nargs="+", type=Path)
    return parser.parse_args(argv)


def _usage_error(args: argparse.Namespace) -> str:
    if args.warn >= args.fail:
        return "--warn must be lower than --fail"
    missing = [str(p) for p in args.paths if not p.exists()]
    return "path not found: " + ", ".join(missing) if missing else ""


def main(argv: Sequence[str]) -> int:
    args = _parse_args(argv)
    problem = _usage_error(args)
    if problem:
        print(problem, file=sys.stderr)
        return 2
    sized = tuple((p, count_lines(p)) for p in iter_files(args.paths))
    graded = tuple((p, n, classify(n, args.warn, args.fail)) for p, n in sized)
    for path, lines, grade in graded:
        if grade != "ok":
            limit = args.fail if grade == "fail" else args.warn
            print(f"{grade.upper():4}  {path}: {lines} lines (> {limit})")
    warns = sum(1 for _, _, g in graded if g == "warn")
    fails = sum(1 for _, _, g in graded if g == "fail")
    print(f"checked {len(graded)} files: {warns} warnings, {fails} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
