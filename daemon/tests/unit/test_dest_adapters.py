"""Executors (docs/14 §12.2, §4.3, §11): the clipboard through a fake pbcopy, files in a temporary $HOME."""

from __future__ import annotations

import os
import subprocess
from dataclasses import replace
from pathlib import Path

import pytest

from cpad.dest.adapters import PBCOPY, ClipAdapter, FileAdapter, Probe, execute, file_line, make_adapters, probe
from cpad.dest.model import Capture, Delivery, DeliveryResult, Dest, FileOpts
from cpad.dest.paths import PathRefused, append_line, check_place, check_spec, target_path

ITEM = "3QZK1M7-0000C-0005"
TEXT = "周五前给 Alice 发报价"
CAP = Capture(ITEM, "typed", "file.sb", "key", "cpa", TEXT, "2026-10-22 09:14")
LINE = f"- 2026-10-22 09:14 #cpa {TEXT} ^cpa-{ITEM.lower()}"


class FakeRun:
    def __init__(self, result=None, raises: BaseException | None = None):
        self.calls: list[tuple[list[str], dict]] = []
        self.result = result if result is not None else subprocess.CompletedProcess([PBCOPY], 0, b"", b"")
        self.raises = raises

    def __call__(self, argv, **kwargs):
        self.calls.append((list(argv), kwargs))
        if self.raises is not None:
            raise self.raises
        return self.result


def clip_delivery(text: str = TEXT) -> Delivery:
    return Delivery(f"{ITEM}:clip", replace(CAP, dest="clip", text=text), Dest("clip", "clip", "剪贴板", 5, key="c"))


def file_dest(root: str | Path, path: str = "inbox/cardputer.md") -> Dest:
    return Dest("file.sb", "file", "sb", 7, leaves_mac=False, file=FileOpts(str(root), path))


def file_delivery(dest: Dest, cap: Capture = CAP) -> Delivery:
    return Delivery(f"{cap.ref}:{dest.id}", cap, dest)


@pytest.fixture
def home(tmp_path) -> Path:
    path = (tmp_path / "home").resolve()
    (path / "notes" / "inbox").mkdir(parents=True)
    return path


# --- clipboard -------------------------------------------------------------------------------


def test_pbcopy_gets_the_text_on_stdin_only():
    run = FakeRun()
    assert ClipAdapter(run=run).deliver(clip_delivery()) == DeliveryResult(True)
    ((argv, kwargs),) = run.calls
    assert argv == [PBCOPY] and TEXT not in " ".join(argv)
    assert kwargs["input"] == TEXT.encode("utf-8")
    assert kwargs["env"] == {"LANG": "en_US.UTF-8", "LC_ALL": "en_US.UTF-8", "PATH": "/usr/bin:/bin"}
    assert kwargs["timeout"] == 2.0 and kwargs["check"] is False and kwargs["capture_output"] is True


@pytest.mark.parametrize(
    ("run", "code"),
    [(FakeRun(raises=FileNotFoundError()), "E_DEST_MISSING"),
     (FakeRun(raises=subprocess.TimeoutExpired(PBCOPY, 2.0)), "E_DEST_TIMEOUT"),
     (FakeRun(raises=PermissionError()), "E_DEST_IO"),
     (FakeRun(subprocess.CompletedProcess([PBCOPY], 1, b"", b"boom")), "E_DEST_IO")],
)  # fmt: skip
def test_clipboard_failures_are_never_retried_automatically(run, code):
    assert ClipAdapter(run=run).deliver(clip_delivery()) == DeliveryResult(False, code, retryable=False)


def test_clip_probe_checks_the_binary_without_running_it(tmp_path):
    exe = tmp_path / "pbcopy"
    exe.write_text("#!/bin/sh\n")
    exe.chmod(0o755)
    run = FakeRun()
    assert ClipAdapter(run=run, exe=str(exe)).probe(clip_delivery().dest) == Probe("ok")
    assert ClipAdapter(run=run, exe=str(tmp_path / "missing")).probe(clip_delivery().dest).state == "missing"
    assert run.calls == []


# --- file line -------------------------------------------------------------------------------


def test_file_line():
    assert file_line(CAP) == (LINE, f"^cpa-{ITEM.lower()}")
    odd = replace(CAP, project=None, text="a\tb\nc ", stamp="~2026-10-22 09:20")
    assert file_line(odd)[0] == f"- ~2026-10-22 09:20 a b c ^cpa-{ITEM.lower()}"


# --- file adapter ----------------------------------------------------------------------------


def test_appends_one_private_line(home):
    adapter = FileAdapter(home)
    result = adapter.deliver(file_delivery(file_dest("~/notes")))
    target = home / "notes" / "inbox" / "cardputer.md"
    assert result == DeliveryResult(True, ref=f"^cpa-{ITEM.lower()}")
    assert target.read_text(encoding="utf-8") == LINE + "\n"
    assert oct(target.stat().st_mode & 0o777) == "0o600"


def test_the_same_delivery_twice_writes_one_line(home):
    adapter = FileAdapter(home)
    dest = file_dest(home / "notes")
    assert adapter.deliver(file_delivery(dest)).ok and adapter.deliver(file_delivery(dest)).ok
    assert (home / "notes" / "inbox" / "cardputer.md").read_text(encoding="utf-8").count("\n") == 1


def test_an_existing_file_gets_a_newline_first_and_is_never_rewritten(home):
    target = home / "notes" / "inbox" / "cardputer.md"
    target.write_text("# Inbox\n- old line", encoding="utf-8")
    assert FileAdapter(home).deliver(file_delivery(file_dest("~/notes"))).ok
    assert target.read_text(encoding="utf-8") == f"# Inbox\n- old line\n{LINE}\n"


def test_a_marker_quoted_mid_line_does_not_count(home):
    target = home / "notes" / "inbox" / "cardputer.md"
    target.write_text(f"see ^cpa-{ITEM.lower()} later\n", encoding="utf-8")
    assert FileAdapter(home).deliver(file_delivery(file_dest("~/notes"))).ok
    assert target.read_text(encoding="utf-8").endswith(LINE + "\n")


def _refused(home: Path, dest: Dest) -> None:
    before = sorted(p for p in home.rglob("*"))
    assert FileAdapter(home, denied=(home / "cpa",)).deliver(file_delivery(dest)) == DeliveryResult(
        False, "E_DEST_PATH", retryable=False
    )
    assert sorted(p for p in home.rglob("*")) == before


def test_a_symlinked_folder_out_of_root_is_refused(home, tmp_path):
    outside = tmp_path / "outside"
    outside.mkdir()
    (home / "notes" / "link").symlink_to(outside)
    _refused(home, file_dest("~/notes", "link/a.md"))
    assert list(outside.iterdir()) == []


def test_a_symlinked_file_is_refused(home, tmp_path):
    victim = tmp_path / "victim.md"
    victim.write_text("keep\n", encoding="utf-8")
    (home / "notes" / "inbox" / "cardputer.md").symlink_to(victim)
    _refused(home, file_dest("~/notes"))
    assert victim.read_text(encoding="utf-8") == "keep\n"


@pytest.mark.parametrize(
    ("root", "path"),
    [("~/Documents", "a.md"), ("~/Library/Mobile Documents/x", "a.md"), ("~", "Desktop/a.md"),
     ("~/notes", "run.sh"), ("~/notes", ".zshrc"), ("~/notes", "inbox/.hidden.md"), ("~/notes", "../a.md"),
     ("~/notes", "missing/a.md"), ("~/cpa", "a.md"), ("notes", "a.md")],
)  # fmt: skip
def test_refused_paths_write_nothing(home, root, path):
    (home / "Documents").mkdir()
    (home / "Library" / "Mobile Documents" / "x").mkdir(parents=True)
    (home / "Desktop").mkdir()
    (home / "cpa").mkdir()
    _refused(home, file_dest(root, path))


def test_an_absolute_root_inside_a_denied_folder_is_refused(home):
    (home / "Documents" / "vault").mkdir(parents=True)
    _refused(home, file_dest(home / "Documents" / "vault", "a.md"))
    _refused(home, file_dest(home, "Documents/vault/a.md"))


def test_a_fifo_is_not_a_file(home):
    os.mkfifo(home / "notes" / "inbox" / "cardputer.md")
    assert FileAdapter(home).deliver(file_delivery(file_dest("~/notes"))).code == "E_DEST_PATH"


def test_an_unwritable_file_is_a_retryable_io_error(home):
    target = home / "notes" / "inbox" / "cardputer.md"
    target.write_text("", encoding="utf-8")
    target.chmod(0o400)
    try:
        assert FileAdapter(home).deliver(file_delivery(file_dest("~/notes"))) == DeliveryResult(
            False, "E_DEST_IO", retryable=True
        )
    finally:
        target.chmod(0o600)


def test_a_dest_without_file_options_is_refused(home):
    dest = replace(file_dest("~/notes"), file=None)
    assert FileAdapter(home).deliver(file_delivery(dest)).code == "E_DEST_PATH"


# --- probes ----------------------------------------------------------------------------------


def test_file_probe_never_writes(home):
    adapter = FileAdapter(home)
    target = home / "notes" / "inbox" / "cardputer.md"
    assert adapter.probe(file_dest("~/notes")) == Probe("ok")
    assert not target.exists()
    assert adapter.probe(file_dest("~/notes", "nowhere/a.md")).state == "needs_setup"
    assert adapter.probe(replace(file_dest("~/notes"), file=None)).state == "needs_setup"
    assert adapter.probe(file_dest("~/notes", "a.sh")).state == "needs_setup"


def test_file_probe_sees_an_unwritable_target(home):
    target = home / "notes" / "inbox" / "cardputer.md"
    target.write_text("", encoding="utf-8")
    target.chmod(0o400)
    try:
        assert FileAdapter(home).probe(file_dest("~/notes")).state == "needs_setup"
    finally:
        target.chmod(0o600)


def test_probe_per_kind(home):
    adapters = make_adapters(home)
    assert set(adapters) == {"clip", "file", "url", "app"}
    assert probe(Dest("paste", "paste", "x", 6), adapters).state == "unavailable"
    assert probe(Dest("paste.ed", "paste", "ed", 9), adapters).state == "unavailable"
    assert probe(Dest("daily", "daily", "x", 1, key="n"), adapters) == Probe("ok")
    assert "project" in probe(Dest("queue", "queue", "x", 3, key="q"), adapters).reason
    assert probe(file_dest("~/notes"), adapters) == Probe("ok")
    assert probe(Dest("clip", "clip", "x", 5), {}).state == "unavailable"


# --- executor --------------------------------------------------------------------------------


def test_execute_runs_the_adapter_of_the_kind(home):
    run = FakeRun()
    adapters = {"clip": ClipAdapter(run=run), "file": FileAdapter(home)}
    assert execute(clip_delivery(), adapters).ok and len(run.calls) == 1
    assert execute(file_delivery(file_dest("~/notes")), adapters).ok
    assert execute(clip_delivery(), {}) == DeliveryResult(False, "E_DEST_MISSING")


# --- path rules directly ---------------------------------------------------------------------


@pytest.mark.parametrize(
    ("root", "path", "reason"),
    [("~/x", "/abs.md", "outside_root"), ("~/x", "a/../b.md", "outside_root"), ("~/x", "a//b.md", "outside_root"),
     ("~/x", ".a/b.md", "hidden"), ("~/.x", "b.md", "hidden"), ("~/x", "b.MDX", "suffix"),
     ("x", "b.md", "outside_root"), ("~/x/../y", "b.md", "outside_root"), ("~", "Downloads/b.md", "denied"),
     ("~/LIBRARY", "b.md", "denied")],
)  # fmt: skip
def test_check_spec_reasons(root, path, reason):
    with pytest.raises(PathRefused) as caught:
        check_spec(FileOpts(root, path))
    assert caught.value.reason == reason


def test_check_spec_accepts_upper_case_suffixes_and_txt():
    check_spec(FileOpts("~/x", "Inbox.MD"))
    check_spec(FileOpts("/Volumes/notes", "inbox.txt"))


@pytest.mark.parametrize(
    ("root", "path"),
    [("Documents", "x.md"), ("documents/sub", "x.md"), ("", "Desktop/x.md"), ("Library/Mobile Documents", "x.md"),
     ("cpa", "x.md"), ("cpa/sub", "x.md"), ("", "cpa/x.md")],
)  # fmt: skip
def test_check_place_refuses_protected_folders_given_as_absolute_paths(home, root, path):
    """What a shell makes of `--root ~/Documents`: the lexical "~" rule alone never sees it."""
    with pytest.raises(PathRefused) as caught:
        check_place(FileOpts(str(home / root) if root else str(home), path), home, denied=(home / "cpa",))
    assert caught.value.reason == "denied"


def test_check_place_accepts_other_folders_and_applies_the_lexical_rules(home):
    check_place(FileOpts(str(home / "notes"), "inbox.md"), home, denied=(home / "cpa",))
    check_place(FileOpts("~/notes", "inbox.md"), home)
    check_place(FileOpts("/Volumes/notes", "inbox.txt"), home)
    with pytest.raises(PathRefused) as caught:
        check_place(FileOpts(str(home / "notes"), "x.sh"), home)
    assert caught.value.reason == "suffix"


def test_target_path_resolves_inside_root(home):
    assert target_path(FileOpts("~/notes", "inbox/a.md"), home) == home / "notes" / "inbox" / "a.md"
    with pytest.raises(PathRefused) as caught:
        target_path(FileOpts("~/notes", "none/a.md"), home)
    assert caught.value.reason == "no_parent"


def test_append_line_refuses_a_symlink_itself(home, tmp_path):
    victim = tmp_path / "v.md"
    victim.write_text("", encoding="utf-8")
    link = home / "notes" / "l.md"
    link.symlink_to(victim)
    with pytest.raises(PathRefused) as caught:
        append_line(link, "x", "^m")
    assert caught.value.reason == "symlink" and victim.read_text(encoding="utf-8") == ""
