"""Confined reads (docs/15 §3.8, ADR 0004 rule 5): traversal, a symlink at every level, dot files, non-regular and
oversized files are each refused."""

from __future__ import annotations

import os
import shutil

import pytest

from cpad.archive import fs
from cpad.archive.fs import ArchiveError, Root

PARTS = ("recordings", "2026-10-21", "1402-3qzk1m7-00042-0a1c", "summary.md")


@pytest.fixture
def tree(tmp_path):
    notes = tmp_path / "notes"
    target = notes.joinpath(*PARTS)
    target.parent.mkdir(parents=True)
    target.write_text("纪要", encoding="utf-8")
    return notes


def _code(fn, *args) -> str:
    with pytest.raises(ArchiveError) as exc:
        fn(*args)
    return exc.value.code


def test_reads_bytes_and_text(tree):
    root = Root.at(tree)
    assert fs.read_bytes(root, PARTS, 100) == "纪要".encode()
    assert fs.read_text(root, PARTS, 100) == "纪要"
    assert fs.file_size(root, PARTS) == 6


def test_invalid_utf8_is_replaced(tree):
    tree.joinpath(*PARTS).write_bytes(b"ok \xff")
    assert fs.read_text(Root.at(tree), PARTS, 100) == "ok �"


def test_root_itself_may_be_a_symlink(tree, tmp_path):
    link = tmp_path / "link"
    link.symlink_to(tree)
    root = Root.at(link)
    assert root.path == tree.resolve()
    assert fs.read_text(root, PARTS, 100) == "纪要"


@pytest.mark.parametrize(
    "parts",
    [("..", "x"), ("recordings/2026-10-21",), (".hidden",), ("",), ("/etc",), ("a\x00",), ("é.md",), ("a b",), ()],
)
def test_unsafe_names_are_refused(tree, parts):
    assert _code(fs.read_bytes, Root.at(tree), parts, 100) == "E_UNSAFE"


@pytest.mark.parametrize("level", range(len(PARTS)))
def test_a_symlink_at_any_level_is_refused(tree, tmp_path, level):
    real = tree.joinpath(*PARTS[: level + 1])
    moved = tmp_path / "outside"
    shutil.move(real, moved)
    real.symlink_to(moved)
    assert _code(fs.read_bytes, Root.at(tree), PARTS, 100) == "E_UNSAFE"


def test_listing_through_a_symlinked_directory_is_refused(tree, tmp_path):
    real = tree / "recordings"
    shutil.move(real, tmp_path / "outside")
    real.symlink_to(tmp_path / "outside")
    assert _code(fs.list_names, Root.at(tree), ("recordings",), 100) == "E_UNSAFE"


def test_missing_file_is_not_found(tree):
    assert _code(fs.read_bytes, Root.at(tree), ("2026-10-22.md",), 100) == "E_NOT_FOUND"
    assert _code(fs.read_bytes, Root.at(tree / "nope"), ("2026-10-22.md",), 100) == "E_NOT_FOUND"


def test_fifo_is_refused_without_blocking(tree):
    os.mkfifo(tree / "pipe.md")
    assert _code(fs.read_bytes, Root.at(tree), ("pipe.md",), 100) == "E_UNSAFE"


def test_directory_is_not_a_file(tree):
    assert _code(fs.read_bytes, Root.at(tree), ("recordings",), 100) == "E_UNSAFE"


def test_a_file_in_the_middle_of_the_path_is_refused(tree):
    (tree / "2026-10-21.md").write_text("x")
    assert _code(fs.read_bytes, Root.at(tree), ("2026-10-21.md", "x"), 100) in ("E_UNSAFE", "E_NOT_FOUND")


def test_oversized_file_is_refused(tree):
    assert _code(fs.read_bytes, Root.at(tree), PARTS, 5) == "E_TOO_LARGE"


def test_realpath_outside_the_root_is_refused(tree, monkeypatch):
    root = Root.at(tree)
    monkeypatch.setattr(fs.os.path, "realpath", lambda path, **kw: "/etc/passwd")
    assert _code(fs.read_bytes, root, PARTS, 100) == "E_UNSAFE"
    assert _code(fs.list_names, root, PARTS[:1], 100) == "E_UNSAFE"


def test_unreadable_file_is_an_io_error(tree):
    target = tree.joinpath(*PARTS)
    target.chmod(0)
    try:
        if os.access(target, os.R_OK):  # running as root
            pytest.skip("root can read anything")
        assert _code(fs.read_bytes, Root.at(tree), PARTS, 100) == "E_IO"
    finally:
        target.chmod(0o600)


def test_list_names_skips_dot_names_symlinks_and_unsafe_names(tree, tmp_path):
    (tree / ".obsidian").mkdir()
    (tree / "2026-10-21.md").write_text("x")
    (tree / "with space.md").write_text("x")
    (tree / "link.md").symlink_to(tmp_path)
    assert fs.list_names(Root.at(tree), (), 100) == ("2026-10-21.md", "recordings")
    assert fs.list_names(Root.at(tree), (), 100, kind="file") == ("2026-10-21.md",)
    assert fs.list_names(Root.at(tree), (), 100, kind="dir") == ("recordings",)


def test_list_names_caps_the_directory(tree):
    for i in range(5):
        (tree / f"n{i}.md").write_text("x")
    assert _code(fs.list_names, Root.at(tree), (), 3) == "E_TOO_LARGE"


def test_list_names_of_a_missing_directory_is_empty(tree):
    assert fs.list_names(Root.at(tree), ("recordings", "2026-01-01"), 100) == ()


def test_chunks_stream_a_file_and_stop_at_the_cap(tree):
    big = tree / "big.wav"
    big.write_bytes(b"a" * 300_000)
    assert b"".join(fs.chunks(Root.at(tree), ("big.wav",), 300_000)) == b"a" * 300_000
    with pytest.raises(ArchiveError):
        list(fs.chunks(Root.at(tree), ("big.wav",), 1000))
